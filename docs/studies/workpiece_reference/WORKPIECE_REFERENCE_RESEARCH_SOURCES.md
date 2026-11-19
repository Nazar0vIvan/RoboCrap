# Sources: robotic CAM workpiece frames and planar probing

Research date: 2026-11-19. Companion to
[the research report](WORKPIECE_REFERENCE_RESEARCH.md).
All external sources below are manufacturer documentation, original research or an official
standards catalogue. An operating procedure establishes what its named product documents;
it does not establish compatibility or accuracy for RoboCrap. No interviews or vendor enquiries
were conducted. No vendor software or robot procedure was run.

"Read" identifies the inspected extent. A selected-section review is not a full-manual review.
The report paraphrases the relevant facts; it does not reproduce the source procedures wholesale.

## S01

**KUKA KSS 8.3, Operating and Programming Instructions for System Integrators**, KSS 8.3 SI V4,
issued 2015-01-14. Local supplied manual, 491 pages:
[PDF](<../../manuals/KUKA/KUKA KSS 8.3. Operating and Programming Instructions for SI [ENG].pdf>).
Read: section 4.13, pp.64-65; 5.10.3, pp.131-134; stationary-tool explanation pp.136-138;
external-kinematic base explanation pp.144-145. Printed and one-based PDF pages agree.
Supports: WORLD/ROBROOT/BASE distinctions, three-point teaching, indirect identification using
four known points, numerical entry and attachment to an external mechanism.
Limit: the inspected base-teaching section does not supply automatic probe acquisition or a
three-plane fitting procedure. Its use of "calibration" for a frame does not mean robot-model identification.

## S02

**ABB RobotStudio API, RsWorkObject**, unversioned web reference:
[class and properties](https://developercenter.robotstudio.com/api/robotstudio/api/ABB.Robotics.RobotStudio.Stations.RsWorkObject.html).
Read: frame and ownership properties. Supports: object/user-frame hierarchy, programmed positions
in the object frame, and distinction between fixed and mechanical-unit-driven user frames.
Limit: a data model, not a probing application or accuracy specification.

## S03

**ABB RAPID Instructions, Functions and Data Types**, RobotWare 5.13,
3HAC16581-1 Revision J, 2004-2010:
[PDF](https://library.e.abb.com/public/688894b98123f87bc1257cc50044e809/Technical%20reference%20manual_RAPID_3HAC16581-1_revJ_en.pdf).
Read: SearchL, pp.416-419; DefAccFrame, pp.816-818; DefFrame, pp.822-824;
wobjdata, pp.1224-1225. One-based PDF page = printed page + 2.
Supports: corresponding-point frame estimation, signal-triggered position capture and sensor-based
workobject relocation. Limit: versioned controller facilities, not a complete CAD-plane workflow.

## S04

**ABB SmarTac/PIB Product Manual**, S4Cplus, 503497-502, 2001-03-06:
[PDF](https://library.e.abb.com/public/8b41fed7c83d3466c125766d003a0d0a/503497-502.pdf).
Read: introduction p.5; section 5.5 p.30; 5.6 pp.35-36; 5.7 pp.38-43; search description pp.47-48.
Supports: automatic contact searches and workobject displacement. Limit: historical welding
wire/gas-cup sensing; its illustrated plate procedure is not general six-dimensional ruby-probe metrology.

## S05

**RoboDK, Define a Reference Frame**, unversioned:
[procedure](https://robodk.com/doc/en/General-Define-Reference-Frame.html).
Read: reference-frame definition procedure, including joint/XYZ inputs and listed methods.
Supports: physical measurement of a station frame. Limit: the page lists a six-point option
without specifying enough mathematics to equate it to three independent plane fits.

## S06

**RoboDK, Set Reference Frame**, unversioned:
[instruction semantics](https://robodk.com/doc/en/Robot-Programs-Set-Reference-Frame.html).
Read: instruction description. Supports: following Cartesian targets refer to the selected frame;
named controller counterparts include KUKA BASE, ABB workobject, Fanuc UFRAME and Motoman FRAME.
Limit: not a description of each controller's internal kinematic calculations.

## S07

**RoboDK, Setup for Machining**, unversioned:
[procedure](https://robodk.com/doc/en/Robot-Machining-Setup-Machining.html).
Read: machining reference, CAD attachment and placement steps.
Supports: a dedicated machining frame and optional CAD under it. Limit: an example setup,
not proof that every machining job requires loading a CAD model.

## S08

**RoboDK, Robot Machining Project**, unversioned:
[procedure](https://robodk.com/doc/en/Robot-Machining-Robot-Machining-project.html).
Read: input/reference/tool selection and program-update sections.
Supports: matching toolpath and CAD coordinates to one reference, followed by robot-program
calculation. Limit: no guarantee that an already exported controller file is automatically replaced
after a later frame correction.

## S09

**RoboDK, Probing Add-in**, unversioned:
[documentation](https://robodk.com/doc/en/Addin-Probing.html).
Read: complete page, especially Requirements, Creating/Optimizing a Project and Calibrate Frame.
Supports: CAD-selected contact positions, measured-datum refitting and part-frame correction.
Limits: optional licensed add-in; driver and Renishaw execution styles differ; no RSI implementation
or complete alignment objective is documented.

## S10

**SprutCAM X 17, Setting the Coordinate System of the Tool and Workpiece**:
[robot procedure](https://kb.sprutcam.com/docs/SprutCAM/17/en/10262.html).
Read: defining the user coordinate system, steps 1-3.
Supports: matching three real taught points with CAD points and entering measured workpiece placement.
Limit: corresponding-point teaching, not arbitrary contacts on fitted planes. Version 17 is the
inspected documentation, not an assertion about the latest release.

## S11

**SprutCAM X 17, Positioning of Part at Machine**:
[placement](https://kb.sprutcam.com/docs/SprutCAM/17/en/10066.html), and
**Workpiece Coordinate System (G54-G59)**:
[WCS settings](https://kb.sprutcam.com/docs/SprutCAM/17/en/10068.html).
Read: both relevant settings descriptions. Supports: separating full part placement from NC-zero
selection. Limit: the general WCS geometric-CS control uses its origin, not its orientation;
that control alone does not perform a six-component workpiece placement.

## S12

**SprutCAM X 17, Use of Prepared Measuring Cycles**:
[probing](https://kb.sprutcam.com/docs/SprutCAM/17/en/1415.html),
**Creating Own Probing Cycles**:
[templates](https://kb.sprutcam.com/docs/SprutCAM/17/en/1411.html), and
**Types of Measuring Cycles**:
[cycle geometry](https://kb.sprutcam.com/docs/SprutCAM/17/en/1414.html).
Read: part probing, transition, output and offset-property descriptions.
Supports: CAD-selected probing operations, configurable links and postprocessed cycles; robot
availability is explicitly stated for part/tool probing. Limit: general offset options do not
prove a particular KUKA postprocessor returns a full measured frame to the CAM scene.

## S13

**Autodesk, PowerMill Robot Postprocessor 3.0**:
[technical specification](https://www.autodesk.com/sites/default/files/file_downloads/PowerMill-Robot-PostProcessors.pdf).
Read: section 2.13 p.8; sections 3.1.1-3.1.2 pp.13-14; PDF indices 7, 12 and 13.
Supports: emitted fixture-origin transforms and different controller/postprocessor responsibilities
for linked versus unlinked external axes. Limit: this external-axis distinction is not a static
part-probing algorithm or proof of the latest PowerMill Robot behavior.

## S14

**Autodesk PowerInspect 2025, To Probe the Initial Alignment**:
[procedure](https://help.autodesk.com/cloudhelp/2025/ENU/PWRI-ReferenceHelp/files/GUID-B0659B4E-4681-431D-8BF4-155D3A0FBA73.htm).
Read: complete procedure. Supports: six-contact initial alignment, datum offsets and a PCS-to-machine
transform. Limit: explicitly scoped to CNC and Manual configurations; not evidence of robot integration.

## S15

**Autodesk PowerInspect 2025**:
[probing methods](https://help.autodesk.com/cloudhelp/2025/ENU/PWRI-ReferenceHelp/files/GUID-47008C1D-D815-45A1-8EF3-11296F9CE994.htm),
[UserDefined method](https://help.autodesk.com/cloudhelp/2025/ENU/PWRI-ReferenceHelp/files/GUID-97C5E487-F86E-41A1-8E02-4C222B55BC16.htm),
[Raster method](https://help.autodesk.com/cloudhelp/2025/ENU/PWRI-ReferenceHelp/files/GUID-E2D54988-97BE-4FC5-A238-676CEE8FA8CB.htm).
Read: contact selection, intermediate positions, method eligibility and raster bounds.
Supports: separate contact/transfer positions and scanning-head requirements. Limit: inspection
software mechanics, not proof of RSI execution or automatic handling of unknown vice geometry.

## S16

**Autodesk PowerInspect 2024, Optimizing an Alignment**:
[procedure](https://help.autodesk.com/cloudhelp/2024/ENU/PWRI-GettingStarted/files/GUID-13F9C064-2B7B-44A6-9722-3BF87E757F70.htm).
Read: measured-group selection, fitting and application steps.
Supports: refinement of an existing alignment using measured data. Limit: the documented consumer
is inspection reporting, not automatic correction of a PowerMill Robot machining job.

## S17

**Renishaw, RCS P-series Macro Instructions for KUKA Robot Controllers**,
H-6852-8030, issued November 2024:
[PDF](https://www.renishaw.com/resourcecentre/download/user-guide-rcs-p-series-macro-instructions-for-kuka-robot-controllers--140555?userLanguage=en).
Read: relevant parts of pp.11-19, 34-37, 46-49 and glossary; 72-page manual not read in full.
Supports: compensated measured-feature alignment and writing a controller frame.
Limits: simple PPP requires nominal orthogonality; proprietary server/macros/licences;
WP-500, KSS 8.3 and RSI 3.3 compatibility not established.

## S18

**Hexagon PC-DMIS 2022.2, Alignment Overview**, 2022-08-05:
[overview](https://docs.hexagonmi.com/pcdmis/2022.2/en/helpcenter/mergedProjects/core/18_alignment_topics/Alignment_Overview.htm),
and **2021.1, Step 1: Measure the Alignment Features**, 2021-02-22:
[feature measurements](https://docs.hexagonmi.com/pcdmis/2021.1/en/helpcenter/mergedProjects/core/18_alignment_topics/Step_1_Measure_the_Alignment_Features.htm).
Read: complete pages. Supports: level/rotate/origin order, six constrained pose freedoms,
and construction using three hits on a plane, two on a line and one point. Limit: CMM alignment, not a robot interface.

## S19

**Hexagon PC-DMIS 2022.1, The Best Fit Alignment Methods**, 2022-02-16:
[methods](https://docs.hexagonmi.com/pcdmis/2022.1/en/helpcenter/mergedProjects/core/18_alignment_topics/The_Best_Fit_Alignment_Methods.htm).
Read: complete page. Supports: differences between least-squares point matching, vector least
squares and MinMax. Limit: a software method description, not proof that every vendor's "best fit"
uses the same objective or weights.

## S20

**Hexagon PC-DMIS 2022.1, Constructing a Primary Datum Plane**, 2022-02-16:
[construction](https://docs.hexagonmi.com/pcdmis/2022.1/en/helpcenter/mergedprojects/core/16_const_topics/Constructing_a_Primary_Datum.htm).
Read: complete page. Supports: distinction between an initial supporting fit and an external
datum plane tangent to material high points. Limit: this implementation is not a complete
normative definition or evidence of compliance by a new solver.

## S21

**Hexagon PC-DMIS, Iterative Alignment**:
[2021.2 procedure](https://docs.hexagonmi.com/pcdmis/2021.2/en/helpcenter/mergedprojects/core/18_alignment_topics/Creating_an_Iterative_Alignment.htm),
[2023.1 point-target radius](https://docs.hexagonmi.com/pcdmis/2023.1/en/helpcenter/mergedProjects/core/18_alignment_topics/Point_Target_Radius.htm),
[2023.1 Meas All Once](https://docs.hexagonmi.com/pcdmis/2023.1/en/helpcenter/mergedProjects/core/18_alignment_topics/Meas_All_Once.htm).
Read: complete pages. Supports: initial manual location followed by automatic remeasurement and
target-tolerance iteration. Limit: CMM practice; robot transfer clearance remains unproven.

## S22

**Phillips, Borchardt and Caskey, Measurement Uncertainty Considerations for Coordinate
Measuring Machines**, NISTIR 5170, April 1993:
[PDF](https://nvlpubs.nist.gov/nistpubs/Legacy/IR/nistir5170.pdf).
Read: sections 5.2-5.4, printed pp.12-18, and relevant Appendix A pp.26-28.
Supports: probing effects, sampling of imperfect form, error amplification and fixture effects.
Limit: mechanisms, not a numerical uncertainty budget for the present robot or workpiece.

## S23

**Shakarji and Srinivasan, Theory and Algorithms for Weighted Total Least-Squares Fitting of
Lines, Planes, and Parallel Planes to Support Tolerancing Standards**, JCISE 13, 031008,
September 2013, DOI 10.1115/1.4024854:
[NIST-hosted paper](https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=912180).
Read: introduction, section 2.1 and section 4.1; not every proof.
Supports: multi-patch surfaces, uneven-sampling influence and weighted plane fitting.
Limit: it does not prescribe how application-specific weights should be selected or reconstruct
unseen surface form.

## S24

**Renishaw, TP7 High Accuracy Touch Trigger Probe System User Guide**, H-1000-5010-06-G,
issued October 2024:
[PDF](https://www.renishaw.com/resourcecentre/download/user-guide-tp7-high-accuracy-touch-trigger-probe-system--140396?userLanguage=en).
Read: printed pp.10, 19 and 21-23. Supports: pretravel, directional variation, speed-dependent
qualification and probe-versus-complete-system performance. Limit: TP7 mechanisms and specifications
must not be assigned numerically to WP-500.

## S25

**Hexagon PC-DMIS 2023.1, Constructing a Best Fit or Best Fit Recompensate Plane**, 2023-03-08:
[procedure](https://docs.hexagonmi.com/pcdmis/2023.1/en/helpcenter/mergedprojects/core/16_const_topics/Constructing_a_Best_Fit_or_Best_Fit_Recompensate_Plane.htm).
Read: complete page. Supports: distinguishing surface-point input from probe-centre input with
compensation during fitting. Limit: does not specify the WP-500 trigger correction model.

## S26

**ISO 5459:2024**, edition 3, October 2024:
[official catalogue and abstract](https://www.iso.org/standard/87855.html).
Read: catalogue/abstract only, not the full standard. Supports: edition status and scope of datum
specification versus verification. It cannot support a claim that this research or a future solver
implements all ISO datum rules. No ASME compliance conclusion is made from front matter or previews.

## S27

**Franaszek, Cheok, van Wyk and Marvel, Improving 3D Vision-Robot Registration for Assembly
Tasks**, NISTIR 8300, April 2020:
[publication](https://www.nist.gov/publications/improving-3d-vision-robot-registration-assembly-tasks),
[PDF](https://nvlpubs.nist.gov/nistpubs/ir/2020/NIST.IR.8300.pdf).
Read: abstract, introduction, section 3.1 p.5, configuration observations pp.9 and 20-21;
selected sections, not every result. Supports: separating registration-fit error from error at
independent targets and the importance of robot configuration. Limit: vision/assembly experiments,
not a quantitative prediction for this machining cell.

## S28

**Dehghani, McKenzie, Irani and Ahmadi, Robot-mounted Sensing and Local Calibration for
High-accuracy Manufacturing**, Robotics and Computer-Integrated Manufacturing 79, 102429,
DOI 10.1016/j.rcim.2022.102429:
[publisher page](https://www.sciencedirect.com/science/article/pii/S0736584522001144).
Read: public abstract and introductory preview, not the full paywalled article.
Supports: a published case combining local robot/sensor/tool calibration, workpiece scanning
and registration before peripheral machining. Limit: its performance does not transfer to KR10/WP-500;
it is not evidence for the exact proposed planar-datum procedure.
