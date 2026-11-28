# Ponytail

Use the ponytail skill for this task. Follow its principles: make the smallest complete change, reuse existing code, avoid overengineering, and preserve correctness, safety

---

# OpenTab

1) Установиь

```
py -m pip install opentab-ai
```

2) Открыть в браузере

```
py -m opentab web
```

3) Открыть в PowerShell

```
py -m opentab
```

4. Экспорт сессии OpenTab

```
cd "$env:USERPROFILE\Downloads"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\export-codex-session.ps1
```

---

# RoboCrap-snapshot

Получить RoboCrap-snapshot - НАЧАЛО нового чата ChatGPT

```
cd "$env:USERPROFILE\Downloads"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Export-RoboCrapSnapshot.ps1 -ProjectRoot "E:\Qt\QtProjects\RoboCrap" -Initial
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Export-RoboCrapSnapshot.ps1 -ProjectRoot "D:\Qt\QtProjects\RoboCrap" -Initial
```

Получить RoboCrap-snapshot - ПРОДОЛЖЕНИЕ чата ChatGPT

```
cd "$env:USERPROFILE\Downloads"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Export-RoboCrapSnapshot.ps1 -ProjectRoot "E:\Qt\QtProjects\RoboCrap"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Export-RoboCrapSnapshot.ps1 -ProjectRoot "D:\Qt\QtProjects\RoboCrap"
```

Дерево проекта:

```
cd "$env:USERPROFILE\Downloads"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Show-RoboCrapTree.ps1"
```

---

Есть следующая проблема. Сейчас уже реализована создание Refine Job по точкам для трёх плоскостей + и анимация сканирования этих точек 
Генерируемая траектория сканирования делиться на две части: 

1) Часть, которая исполняется встроенными KRL командами (LIN, CIRC и пр.)
2) Часть, которая исполняется RSI

На сколько я могу судить, это ведет к 3 вариантам решения вопроса доставки желаемого движения от приложения до KRC4: 

1) Составление шаблона KRL программы с уже заранее установленной последовательностью выполнения PTP, LIN, CIRC ... RSI_MOVECORR()
2) Генерация src/dat файлов, например, scan.src/dat на стороне приложения и каким-то образом доставка их до KRC4. При этом для автоматического запуска через cell.src названия файлов scan.src/dat одно и тоже для разных траекторий сканирования, но содержимое разное для каждой траектории.
3) Генерация всей траектории сканирования через RSI

---

По ходу выполнения плана, я заметил следующее:
1) Вроде мы говорили об сканировании и калибровке, но изменялись такие файлы как scenemachiningpreparaton.h/cpp и скорее всего ещё наподобие им, хотя по названию они к сканировании и калиброке никакого отношение иметь не должен. Это наводит на опасение о наличии GodObject, не соблюдения SRP в S.O.L.I.D, да и просто имена файлов не соотвествуют тому, что внутри. Как будто раньше класс был довольно конкретный, а потом его начали использовать в других зонах ответственности и он как бы стал абстракнее, но на деле оставил старое название.
2) Last Calibration должен отображаться только в Calibration submode, когда ничего не выбрано, в остальный submode когда ничего не выбрано, правая панель должна оставать пустой
3) Кнопка Scan должна быть Start Scan, по аналогии со Start Calibration
4) Кнока Resume должна быть справа от Start Scan
5) Предупреждения типо "Workpiece placement changed. Recheck fixture clearance before ... " пусть будут в Status bar
6) Кнопка Save Changes или Create Refine Job не должна растягиваться до максимума
