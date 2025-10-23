# Сторонние зависимости

## RapidYAML (ryml)
- Репозиторий: https://github.com/biojppm/rapidyaml
- Зафиксированная версия: v0.5.0 (через git submodule)
- Команда первоначальной инициализации: `git submodule update --init --recursive`
- Команда для обновления до актуальной ревизии: `git submodule update --remote --merge --recursive`

> После `git pull` не забывайте выполнять `git submodule update --init --recursive`, чтобы подтянуть rapidyaml и вложенный c4core.
