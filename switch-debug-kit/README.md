# Switch Debug Kit

Переносимый комплект для отладки homebrew на **первой Nintendo Switch**. Его можно скопировать в проект своей игры или приложения отдельно от Switch Monitor. Нужны Python 3.11+, установленный devkitPro/devkitA64, libnx и switch-tools. Для штатных локальных C-тестов Windows нужен MinGW-w64 GCC. Python-пакеты из pip не требуются. В Windows используйте Windows-версию Python, а не интерпретатор из `msys64/usr/bin`.

Комплект содержит:

- Приёмник обновлений NRO: аутентификация, SHA-256, проверка формата, прежний файл `.previous`, перезапуск в новом процессе.
- Клиент ПК: передача, запуск, ожидание точного образа, повторно подключаемый журнал, чтение ограниченных журналов падения.
- Самостоятельный ARM-стенд: загрузка нативных тестовых модулей и входных данных без перезапуска стенда, повторные замеры и сравнение результатов.
- Собираемый пример приложения с приёмником, CMake-подключение, тесты и [инструкция интеграции](docs/INTEGRATION.md).

Это исходники для разработчика. Установка ярлыка HOME, отладчик GDB, удалённые кнопки и захват экрана игры в этот комплект **не входят**. Трей Switch Monitor только наблюдает за доступностью служб; код приёмника нужно встроить в приложение. Профиль игровых функций должен предоставлять сам проект.

## Быстрый старт

Откройте терминал в этой папке:

```powershell
python host/project.py
```

Или дважды нажмите `setup.cmd`. Будут созданы локальные `project.json` и `data/token.txt`. Повторный запуск не заменяет ключ. Эти файлы исключены из Git и исходного архива.

В `project.json` укажите:

| Поле | Значение |
|---|---|
| `name` | Имя собственного установленного приложения; 1–15 ASCII-символов: буквы, цифры, пробел, `.`, `_`, `-`. Точно совпадает с NACP ярлыка. |
| `author` | Автор из NACP, до 64 байт UTF-8. |
| `title_id` | Собственный ProgramId ярлыка: ровно 16 hex-цифр. `0000000000000000` отключает RUN. |
| `sd_directory` | Одна папка под `sdmc:/switch/`, например `my-game-dev`. Без `/` и `..`. |
| `host` | IPv4 вашей консоли. Пример `127.0.0.1` не обращается к чужому устройству. |

**Для автоматического перезапуска требуется уже установленный ярлык/forwarder вашего проекта.** Его ProgramId и NACP name/author/presence_group_id должны соответствовать конфигу; presence_group_id равен title_id. Ярлык должен запускать `sdmc:/switch/<sd_directory>/bridge.nro`. Комплект не устанавливает и не заменяет заголовки HOME. Не используйте ProgramId другой установленной игры или приложения.

Соберите демонстрацию и стенд:

```powershell
python build.py
```

Или `build.cmd`. SDK ищется в `DEVKITPRO`/`DEVKITA64`, затем `C:/msys64/opt/devkitpro`. Для другого MSYS-каталога задайте `NXDK_MSYS_ROOT`; для другого Python в CMD-скриптах — `NXDK_PYTHON` с полным путём к интерпретатору.

Результаты:

| Файл | Назначение |
|---|---|
| `build/receiver-demo.nro` | Пример приложения с приёмником; не игра. |
| `build/arm-lab.nro` | Отдельный стенд. |
| `build/integer.lab` | Проверочный модуль без импортов. |
| `build/config/reload_project.h` | Один конфиг для приёмника, игры и стенда. |
| `build/sd/switch/<sd_directory>/` | Демонстрационное дерево SD: bridge.nro, game.nro, arm-lab.nro. |
| `build/sha256.json` | Контрольные суммы сборки. |

С нулевым title_id можно собрать и запустить пример/стенд через hbmenu application mode, а также использовать тестовые модули. RUN при этом намеренно недоступен. Для своей игры замените демонстрационные bridge.nro/game.nro на NRO игры с интегрированным приёмником; сначала прочитайте [INTEGRATION.md](docs/INTEGRATION.md).

## Первый запуск и последующие обновления

Один раз откройте hbmenu **в application mode**, включите netloader кнопкой Y и отправьте стенд:

```powershell
python host/reload.py bootstrap build/arm-lab.nro --application LAB
```

Клиент читает ключ из `data/token.txt` и передаёт его через аргументы NRO, не печатая его. При запуске стенда с SD можно положить тот же ключ в `sdmc:/switch/<sd_directory>/token.txt`. Приёмник NRO включается только при явном аргументе `--token=<64hex>`; один файл на SD включает только модульный сервис стенда. Для первого сетевого перехода используйте bootstrap.

Когда настройки собственного ярлыка совпадают и NRO игры уже содержит приёмник:

```powershell
python host/reload.py put BRIDGE path/to/integrated-game.nro
python host/reload.py put GAME path/to/integrated-game.nro
python host/reload.py put LAB build/arm-lab.nro
python host/reload.py run GAME
```

**`GAME` — ваша игра или приложение**, `BRIDGE` — ранний NRO-мост, `LAB` — стенд. C API использует префикс `game_reload_*`. Клиент, приёмник и мост этой версии используют протокол **2**; они несовместимы с протоколом 1. Обновляйте их вместе и заново выполните bootstrap после перехода с прежней версии.

Первый переход из чужого приложения, запущенного через hbmenu, может оставить новый процесс за HOME: откройте свой ярлык один раз. После этого нормальные переходы из собственного ProgramId используют `appletRestartProgram`, сохраняют рабочий сценарий без ручного netloader и подтверждают новый образ по SHA-256.

```powershell
python host/reload.py status
python host/reload.py deploy GAME path/to/new-game.nro
python host/reload.py deploy LAB build/arm-lab.nro
python host/reload.py run GAME
python host/reload.py run LAB
python host/reload.py crash data/crash.txt
```

`run.cmd` передаёт аргументы тому же клиенту; без аргументов показывает status. Успешный PUT означает только запись файла. Успешный deploy дополнительно требует повторного подключения ожидаемого приложения с нужным digest. Для доказательства нового процесса используйте отдельно GDB/системный PID: протокол сам PID не сообщает.

Журнал запускается в отдельном терминале:

```powershell
python host/reload.py logs data/session.log
```

Не запускайте одновременно второй такой приёмник или nxlink `-s`: TCP 28771 должен слушать один процесс. В стенде stdout выводится на экран, stderr идёт в журнал.

## Нативный стенд

```powershell
python host/lab.py status
python host/lab.py --out data/smoke.json smoke
python build.py --module path/to/candidate.c --name candidate
python host/lab.py module build/candidate.lab
python host/lab.py input path/to/capture.bin
python host/lab.py run --iterations 1000 --trials 7
python host/lab.py results
```

`lab.cmd` запускает тот же клиент. См. [LAB.md](docs/LAB.md): ABI, A/B, входы, корректность, ограничения времени и отмены. Стенд загружает доверенный нативный код с правами процесса, а не запускает его в песочнице.

## Проверки и распространение

```powershell
python tests/check.py
```

Или `test.cmd`. Все проверки локальные: конфиг, реальные клиентские сокеты localhost, публикация NRO/ошибки хранения, поток-владелец запуска, поздний запрос при выходе, нулевой ID, ABI модулей и отрицательные образы, сборка обоих NRO. `NXDK_HOST_CC` позволяет выбрать другой MinGW GCC.

Исходный механизм прошёл на Switch цикл игра → стенд → игра, выход с активными GAME/GPU/APU и обновление нового NRO без ручных нажатий. Этот перенос параметризован и проверяется отдельно локально; интеграцию в другую игру и её ярлык нужно подтвердить на её консоли. Происхождение файлов — [PROVENANCE.json](PROVENANCE.json), схема и прежние ошибки — [ARCHITECTURE.md](docs/ARCHITECTURE.md). Задание для другого ИИ — [AGENTS.md](AGENTS.md).

Потеря сети, завершение приложения через HOME, системное падение или выключение могут требовать ручного восстановления. Полной автономности при выключенном приёмнике этот комплект не обещает.

Исходный архив Switch Monitor включает эту папку по явному списку `package-files.txt`. `build/`, `data/`, `project.json`, игровые файлы и секреты в него не попадают. Лицензия исходников — [MIT](LICENSE); SDK libnx/devkitPro устанавливается отдельно и сохраняет свои лицензии.
