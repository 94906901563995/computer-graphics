# Лабораторная работа №1: Основы 3D-графики

Курс «Компьютерная графика», МАИ, каф. 806.

**Вариант 10 — правильный додекаэдр.**

Выполнено базовое задание и дополнительные задания 1–5:

1. Переключение проекции (перспективная / ортографическая) в интерфейсе.
2. Элементы интерфейса для изменения позиции, поворота и растяжения фигуры.
3. Движение и вращение по сложной траектории (эпициклоида + колебания по Y),
   пауза / воспроизведение, скорость и параметры траектории.
4. Изменение цвета фигуры через `ColorEdit`.
5. Процедурные цвета вершин по их положению в локальной системе координат;
   цвет из интерфейса умножается на цвет вершин.

## Сборка

Нужны компилятор C++20, CMake, Ninja, Vulkan SDK (заголовки, загрузчик) и `glslc` в `PATH`.
Сторонние библиотеки (GLFW, vk-bootstrap, VMA, ImGui) CMake скачивает сам.

### Windows (MSYS2 UCRT64)

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,shaderc,vulkan-headers,vulkan-loader} git
cmake --preset debug
cmake --build build-debug --parallel
```

### Windows (Visual Studio 2022 + Vulkan SDK)

```bash
cmake --preset msvc-debug
cmake --build build-debug --parallel
```

### GNU/Linux

```bash
cmake --preset debug
cmake --build build-debug --parallel
```

## Запуск

Рабочая директория должна быть корнем проекта (шейдеры загружаются по относительным путям):

```bash
./build-debug/vulkan-starter-app        # MinGW / Linux
./build-debug/Debug/vulkan-starter-app  # MSVC
```

При сборке через MSYS2 каталог `C:\msys64\ucrt64\bin` должен быть в `PATH`
(из него берётся `libwinpthread-1.dll`).
