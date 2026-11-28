#!/usr/bin/env python3
"""Offline source bridge for the observed ISPSoft 3.19 / AS332T format.
All builds and refreshes write to new directories; never deploy to a PLC.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
import zlib
import hashlib
import shutil

TOKEN = b'PLCnnnnnnnewLineAndSubstitudeeeeeeePLC'

def require(ok, message):
    if not ok:
        raise ValueError(message)



def sha(data):
    return hashlib.sha256(data).hexdigest()



def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]



def decode(data):
    require(len(data) > 170, 'Truncated container')
    require(u32(data, 12) == 1020 and u32(data, 16) == 3000,
            'Unexpected version or open-project header; M1 supports only the observed layout')
    offset = u32(data, 0x98)
    require(164 <= offset <= len(data) - 6, 'Invalid source offset')
    require(u32(data, 0x9c) == offset - 164, 'Unexpected preceding block length')
    require(u32(data, 0x94) == len(data) - offset, 'Compressed block length mismatch')
    method, crc = struct.unpack_from('<HI', data, offset)
    require(method == 8, 'Unexpected compression method')
    d = zlib.decompressobj(-15)
    raw = d.decompress(data[offset + 6:]) + d.flush()
    require(d.eof and not d.unused_data and not d.unconsumed_tail, 'Unexpected compressed stream boundary')
    require(len(raw) == u32(data, 0x7c), 'Source size mismatch')
    require(zlib.crc32(raw) == crc, 'Source CRC mismatch')
    require(raw.startswith(b'<PROJECT>\r\n'), 'Unexpected source structure')
    require(b'Active_CodePage=1252\r\n' in raw and b'Prj_ISPVer=3.19 ' in raw,
            'M1 supports only the observed ISPSoft 3.19 CP1252 format')
    require(raw.count(b'\r\n') == raw.count(b'\r') == raw.count(b'\n'), 'Unexpected line endings')
    raw.decode('cp1252')
    return offset, raw



def tags(data, tag):
    return list(re.finditer(rb'(?ms)^<' + tag + rb'>\r\n(.*?)^</' + tag + rb'>\r?$', data))



def prop(data, name):
    m = re.search(rb'(?m)^' + name + rb'=([^\r\n]*)\r$', data)
    require(m is not None, f'Missing {name!r}')
    return m[1].decode('cp1252')



def safe(s):
    require(re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', s) is not None, 'Unexpected identifier: ' + s)
    return s



def one(data, tag):
    found = tags(data, tag)
    require(len(found) == 1, f'Expected one {tag!r}')
    return found[0]



def sections(raw):
    entries = {}
    def add(path, start, end, dut=False):
        require(path.casefold() not in {x.casefold() for x in entries}, f'Duplicate section: {path}')
        entries[path] = (start, end, dut)
    for p in tags(raw, b'POU'):
        body, base = p[1], p.start(1)
        name = safe(prop(body, b'P_Name'))
        kind, lang = prop(body, b'P_type'), prop(body, b'P_Lang')
        require(kind in ('0','1') and lang in ('1','3','4'), f'Unsupported POU: {name}')
        # POU names are unique in the observed project. Folder paths are preserved separately.
        prefix = ('Programs/' if kind == '0' else 'FunctionBlocks/') + name
        v = one(body, b'LOCAL_VAR')
        add(prefix+'/LocalSymbols.txt', base+v.start(1), base+v.end(1))
        if lang == '4':
            c = one(body, b'IL_ST_CODE')
            add(prefix+'/'+name+'.st', base+c.start(1), base+c.end(1))
        elif lang == '3':
            g = one(body, b'NODE_DATA')
            add(prefix+'/SFC-Nodes.txt', base+g.start(1), base+g.end(1))
            for tag, nameprop, langprop, group in [(b'ACT',b'ACT_NAME',b'ACT_LANG','Actions'),(b'TRANS',b'TRANS_NAME',b'TRANS_LANG','Transitions')]:
                for t in tags(body, tag):
                    require(prop(t[1], langprop)=='4', f'Unsupported non-ST {group} in {name}')
                    c = one(t[1], b'IL_ST_CODE')
                    add(prefix+'/'+group+'/'+safe(prop(t[1],nameprop))+'.st',base+t.start(1)+c.start(1),base+t.start(1)+c.end(1))
        else:
            # Preserve the complete serialized LD network region including unknown node properties.
            start=body.find(b'<NETWORK_START>\r\n')
            require(start >= 0 and body.endswith(b'<NETWORK_END>\r\n'), f'Unexpected LD region in {name}')
            add(prefix+'/LD-Networks.txt',base+start,base+len(body))
    for t in tags(raw,b'DUT_NODE'):
        m = list(re.finditer(rb'(?m)^DutContent=([^\r\n]*)\r$', t[1]))
        require(len(m)==1,'Unexpected DUT')
        add('DUT/'+safe(prop(t[1],b'DutName'))+'.st',t.start(1)+m[0].start(1),t.start(1)+m[0].end(1),True)
    for tag in [b'GLOBAL_VAR',b'GLOBAL_AXIS_VAR',b'GLOBAL_ETHERCAT_VAR',b'GLOBAL_EIP_PRODUCED_VAR',b'GLOBAL_EIP_CONSUMED_VAR',b'GLOBAL_HWCONFIG_VAR',b'GLOBAL_OPCUA_VAR',b'SYSTEM_VAR',b'AH_SYSTEM_VAR',b'TASK',b'ProgramFolder',b'FBFolder']:
        t=one(raw,tag);add('Sections/'+tag.decode()+'.txt',t.start(1),t.end(1))
    ordered=sorted(entries.values())
    require(all(a[1]<=b[0] for a,b in zip(ordered,ordered[1:])), 'Overlapping sections')
    require(sum(p.endswith('.st') for p in entries)==len(tags(raw,b'IL_ST_CODE'))+len(tags(raw,b'DUT_NODE')),'Incomplete ST/DUT inventory')
    require(sum(p.endswith('/LocalSymbols.txt') for p in entries)==len(tags(raw,b'POU')),'Incomplete POU inventory')
    return entries



def is_backup(p):
    return p.name.lower().endswith(('.bak','.~bak')) or p.name.lower().startswith('~$')



def file_map(root, backups=True):
    require(root.is_dir(),f'Missing directory: {root}')
    out={}
    for p in sorted(root.rglob('*')):
        require(not p.is_symlink(),f'Symlink not supported: {p}')
        if p.is_file() and (backups or not is_backup(p)):
            key=p.relative_to(root).as_posix()
            require(key.casefold() not in {k.casefold() for k in out},'Case-colliding paths')
            out[key]=p.read_bytes()
    return out



def native_snapshot(root):
    data=file_map(root,backups=False)
    candidates=[p for p in data if '/' not in p and p.lower().endswith('.isp')]
    require(len(candidates)==1,'Expected exactly one top-level .isp')
    name=candidates[0]
    offset,raw=decode(data[name]);return name,data,offset,raw



def hashes(data):return {p:sha(b) for p,b in data.items()}



def changes(before,after):
    return {'added':sorted(after.keys()-before.keys()),'removed':sorted(before.keys()-after.keys()),'modified':sorted(p for p in before.keys()&after.keys() if before[p]!=after[p])}



def dirty(diff):return any(diff.values())



def check_separate(a,b):
    a,b=a.resolve(),b.resolve()
    require(a!=b and a not in b.parents and b not in a.parents,'Directories must be separate, not nested')



def create_files(root, files):
    require(not root.exists(),f'Refusing to overwrite: {root}')
    root.mkdir(parents=True,exist_ok=False)
    try:
        for p,data in files.items():
            target=root/p;target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('xb') as f:f.write(data)
    except BaseException:
        # Only the new directory we created is removed.
        shutil.rmtree(root)
        raise



SCHEMA = 'ispsoft-bridge-v2'
POU_ORDER='Sections/POUOrder.txt'
DUT_ORDER='Sections/DUTOrder.txt'
DEFINITION='Definition.txt'
MARKER=re.compile(rb'(?m)^@@ISP_SECTION:([A-Za-z0-9_./-]+)@@\r\n')


def as_text(b):
    return b.replace(b'\r\n',b'\n').decode('cp1252').encode('utf-8')


def as_native(b):
    require(b'\r' not in b,'Expected normalized LF text')
    return b.decode('utf-8').encode('cp1252').replace(b'\n',b'\r\n')


def line_span(raw, m):
    require(raw[m.end():m.end()+1]==b'\n','Missing record terminator')
    return m.start(),m.end()+1


def contiguous(raw, records):
    require(records,'Empty POU region unsupported')
    spans=[line_span(raw,m) for m in records]
    require(all(a[1]==b[0] for a,b in zip(spans,spans[1:])),'Unrecognized data between records')
    return spans[0][0],spans[-1][1]


def record_name(p):
    name=safe(prop(p,b'P_Name'));kind=prop(p,b'P_type')
    require(kind in ('0','1'),'Unsupported POU kind')
    return ('Programs/' if kind=='0' else 'FunctionBlocks/')+name


def dut_record(name,kind,content):
    require(kind in ('0','1'),'Only observed STRUCT and ENUM kinds supported')
    require(TOKEN not in content,'Reserved DUT marker in source')
    body=content.decode('utf-8').encode('cp1252').replace(b'\n',TOKEN)
    return (f'<DUT_NODE>\r\nDutName={safe(name)}\r\nDutType={kind}\r\nDutContent='.encode('ascii')+
            body+b'\r\n</DUT_NODE>\r\n')


def validate_structure(raw):
    require(prop(one(raw,b'PROJECT')[1],b'Prj_DT')=='AS332T','Only observed AS332T project supported')
    require(prop(one(raw,b'PROJECT')[1],b'Prj_C_SourceCodeLocked')=='FALSE','Locked project unsupported')
    records=tags(raw,b'POU');contiguous(raw,records)
    require(len(records)==raw.count(b'<POU>\r\n'),'POU record count mismatch')
    keys=[record_name(m[1]) for m in records]
    require(len({x.split('/')[1].casefold() for x in keys})==len(keys),'Duplicate POU identifier')
    kinds=[prop(m[1],b'P_type') for m in records]
    require(kinds==sorted(kinds),'Programs must precede FB records')
    for tag,group,kind in [(b'ProgramFolder','Programs','0'),(b'FBFolder','FunctionBlocks','1')]:
        content=one(raw,tag)[1]
        folder_entries=tags(content,b'FolderContent')
        require(b''.join(content[a:b] for a,b in (line_span(content,m) for m in folder_entries))==content,
                'Unknown folder record format')
        names=[]
        for m in folder_entries:
            t=prop(m[1],b'ContentType')
            require(t in ('-1',kind),'Wrong folder entry type')
            if t==kind:names.append(safe(prop(m[1],b'ContentName')))
        expected=[x.split('/')[1] for x in keys if x.startswith(group+'/')]
        require(len(names)==len(set(n.casefold() for n in names)) and set(names)==set(expected),
                f'{group} folder entries do not match definitions')
    tasks=one(raw,b'TASK')[1];programs={x.split('/')[1].casefold() for x in keys if x.startswith('Programs/')}
    for n in re.findall(rb'(?m)^T_PName=([^\r\n]+)\r$',tasks):
        require(n.decode('cp1252').casefold() in programs,'Task references missing program: '+n.decode('cp1252'))
    names=[]
    info=one(raw,b'DUT_INFO')[1]
    for m in tags(info,b'DUT_NODE'):
        names.append(safe(prop(m[1],b'DutName')))
    require(len(names)==len(set(n.casefold() for n in names)),'Duplicate DUT name')
    for m in records:
        body=m[1];require(prop(body,b'P_Security')=='','Protected POU unsupported')
    return keys


def project_text(raw):
    keys=validate_structure(raw);spans=sections(raw)
    src={p:(raw[a:b].replace(TOKEN if dut else b'\r\n',b'\n').decode('cp1252').encode('utf-8')) for p,(a,b,dut) in spans.items()}
    for key,m in zip(keys,tags(raw,b'POU')):
        base=m.start(1);body=m[1];cursor=0;out=bytearray()
        for path,(a,b,dut) in sorted(spans.items(),key=lambda i:i[1][0]):
            if not path.startswith(key+'/'):continue
            require(not dut and base<=a<=b<=m.end(1),'POU span mismatch')
            short=path[len(key)+1:]
            out.extend(body[cursor:a-base]);out.extend(f'@@ISP_SECTION:{short}@@\r\n'.encode('ascii'));cursor=b-base
        out.extend(body[cursor:]);src[key+'/'+DEFINITION]=as_text(bytes(out))
    src[POU_ORDER]=(''.join(k+'\n' for k in keys)).encode('utf-8')
    info=one(raw,b'DUT_INFO')[1];records=tags(info,b'DUT_NODE');order=[];exact=bytearray()
    for m in records:
        n=safe(prop(m[1],b'DutName'));t=prop(m[1],b'DutType');order.append(n+'\t'+t+'\n')
        exact.extend(dut_record(n,t,src['DUT/'+n+'.st']))
    require(bytes(exact)==info,'Unexpected DUT metadata; refusing lossy export')
    src[DUT_ORDER]=''.join(order).encode('utf-8')
    return src


def render_pou(key,src,used):
    group,name=key.split('/');safe(name)
    require(group in ('Programs','FunctionBlocks'),'Unknown POU group')
    prefix=key+'/';path=prefix+DEFINITION;used.add(path)
    body=as_native(src[path]);seen=set()
    def substitute(m):
        short=m[1].decode('ascii')
        require(all(p not in ('','.','..') for p in short.split('/')),'Invalid section path')
        p=prefix+short;require(p not in seen and p!=path,'Duplicate/recursive placeholder')
        seen.add(p);used.add(p)
        return as_native(src[p])
    body=MARKER.sub(substitute,body)
    require(b'@@ISP_SECTION:' not in body,'Unresolved section marker')
    require(record_name(body)==key,'POU header and path disagree: '+key)
    require(body.endswith(b'\r\n'),'POU body must end in newline')
    return b'<POU>\r\n'+body+b'</POU>\r\n'


def assemble(raw,src):
    old_spans=sections(raw);used={POU_ORDER,DUT_ORDER};edits=[]
    for path,(a,b,dut) in old_spans.items():
        if path.startswith('Sections/'):
            used.add(path);edits.append((a,b,as_native(src[path])))
    keys=src[POU_ORDER].decode('utf-8').splitlines()
    require(keys and len({x.casefold() for x in keys})==len(keys),'Duplicate/empty POU order')
    require(all(re.fullmatch(r'(Programs|FunctionBlocks)/[A-Za-z_][A-Za-z0-9_]*',k) for k in keys),'Malformed POU order')
    new=b''.join(render_pou(k,src,used) for k in keys)
    a,b=contiguous(raw,tags(raw,b'POU'));edits.append((a,b,new))
    new=bytearray();seen=set()
    for line in src[DUT_ORDER].decode('utf-8').splitlines():
        name,kind=line.split('\t');safe(name)
        require(name.casefold() not in seen,'Duplicate DUT order name');seen.add(name.casefold())
        path='DUT/'+name+'.st';used.add(path);content=src[path]
        require(re.match(rb'(?i)\s*TYPE\s+'+re.escape(name.encode('ascii'))+rb'\s*:',content),'DUT name mismatch: '+name)
        require((kind=='0' and re.search(rb'(?i)\bSTRUCT\b',content)) or (kind=='1' and re.search(rb'(?i):\s*\(',content)),'DUT kind/source mismatch: '+name)
        new.extend(dut_record(name,kind,content))
    info=one(raw,b'DUT_INFO');edits.append((info.start(1),info.end(1),bytes(new)))
    require(used==set(src),'Unused or unrecognized source files: '+', '.join(sorted(set(src)-used)))
    out=raw
    for a,b,c in sorted(edits,reverse=True):out=out[:a]+c+out[b:]
    require(project_text(out)==src,'Re-extracted text differs from requested source; refusing build')
    return out


def workspace_snapshot(workspace):
    state=json.loads((workspace/'state.json').read_text(encoding='utf-8'))
    require(state.get('schema')==SCHEMA,'Unsupported workspace schema; export a new workspace')
    src={}
    for p,b in file_map(workspace/'src').items():
        t=b.decode('utf-8-sig').replace('\r\n','\n');require('\r' not in t,'Unexpected line endings: '+p);src[p]=t.encode('utf-8')
    return state,src


def export(native,workspace):
    check_separate(native,workspace);name,data,_,raw=native_snapshot(native)
    src=project_text(raw);require(assemble(raw,src)==raw,'Lossless export check failed')
    state={'schema':SCHEMA,'isp':name,'native_sha256':hashes(data),'src_sha256':hashes(src),'raw_sha256':sha(raw)}
    files={'src/'+p:b for p,b in src.items()};files['state.json']=(json.dumps(state,indent=2)+'\n').encode('utf-8')
    create_files(workspace,files);return {'exported':len(src),'workspace':str(workspace),'source_sha256':sha(raw)}


def status(native,workspace):
    check_separate(native,workspace);state,src=workspace_snapshot(workspace);name,data,_,_=native_snapshot(native)
    require(name==state['isp'],'Project name differs from workspace')
    n=changes(state['native_sha256'],hashes(data));s=changes(state['src_sha256'],hashes(src))
    return {'native':n,'text':s,'conflict':dirty(n) and dirty(s),'native_changed':dirty(n),'text_changed':dirty(s)}


def build(native,workspace,output):
    check_separate(native,workspace);check_separate(native,output);check_separate(workspace,output)
    require(not output.exists(),'Refusing to overwrite: '+str(output))
    state,src=workspace_snapshot(workspace);name,data,offset,raw=native_snapshot(native)
    require(name==state['isp'] and hashes(data)==state['native_sha256'] and sha(raw)==state['raw_sha256'],'Native project changed since export. Refusing stale build; refresh/reconcile manually.')
    before=project_text(raw);require(hashes(before)==state['src_sha256'],'Baseline text hashes do not match native source')
    new=assemble(raw,src)
    if new!=raw:
        c=zlib.compressobj(9,zlib.DEFLATED,-15,9);comp=c.compress(new)+c.flush()
        prefix=bytearray(data[name][:offset]);struct.pack_into('<I',prefix,0x7c,len(new));struct.pack_into('<I',prefix,0x94,len(comp)+6)
        built=bytes(prefix)+struct.pack('<HI',8,zlib.crc32(new))+comp
        require(decode(built)==(offset,new),'Container round-trip mismatch')
        require(built[:0x7c]==data[name][:0x7c] and built[0x80:0x94]==data[name][0x80:0x94] and built[0x98:offset]==data[name][0x98:offset],'Unexpected prefix change')
        data[name]=built
    create_files(output,data)
    return {'changed_text':changes(hashes(before),hashes(src)),'output':str(output),'source_crc32':f'{zlib.crc32(new):08X}','opaque_preserved':True,'compilation':'NOT VERIFIED BY SCRIPT'}


def refresh(native,workspace,output):
    check_separate(workspace,output);s=status(native,workspace)
    require(not s['text_changed'],'Workspace contains edits. Refusing refresh until changes are reconciled.')
    return export(native,output)


def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='cmd',required=True)
    for cmd in ('export','status','build','refresh'):
        a=sub.add_parser(cmd);a.add_argument('native',type=Path);a.add_argument('workspace',type=Path)
        if cmd in ('build','refresh'):a.add_argument('output',type=Path)
    a=p.parse_args()
    try:
        if a.cmd=='export':r=export(a.native,a.workspace)
        elif a.cmd=='status':r=status(a.native,a.workspace)
        elif a.cmd=='build':r=build(a.native,a.workspace,a.output)
        else:r=refresh(a.native,a.workspace,a.output)
        print(json.dumps(r,indent=2));return 0
    except (ValueError,OSError,KeyError,UnicodeError,struct.error,zlib.error) as e:
        print(f'REFUSED: {e}',file=sys.stderr);return 2


if __name__=='__main__':sys.exit(main())
