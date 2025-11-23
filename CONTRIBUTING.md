# Contributing to USBODE

Thank you for your interest in contributing to USBODE! This guide will help you get started with the codebase and understand the architecture.

## Table of Contents

1.  [Getting Started](#getting-started)
2.  [Code Style](#code-style)
3.  [Code Structure](#code-structure)
4.  [Addons System](#addons-system)
    *   [Directory Structure](#directory-structure)
    *   [Creating a New Addon](#creating-a-new-addon)
    *   [Registering the Addon](#registering-the-addon)
5.  [Disc Image Plugins](#disc-image-plugins)
    *   [Architecture](#architecture)
    *   [Adding Support for a New Format](#adding-support-for-a-new-format)
    *   [Subchannel Data](#subchannel-data)
6.  [Web Interface](#web-interface)
    *   [Architecture](#architecture-1)
    *   [Templating System](#templating-system)
    *   [Theming System](#theming-system)
    *   [Adding a New Page](#adding-a-new-page)
7.  [Display Service](#display-service)
    *   [Architecture](#architecture-2)
    *   [Adding Support for New Hardware](#adding-support-for-new-hardware)
    *   [Creating a New UI Page](#creating-a-new-ui-page)
8.  [Submitting Changes](#submitting-changes)

## Getting Started

To build the project, please refer to [BUILD.md](BUILD.md) for detailed instructions on setting up the toolchain and environment.

## Code Style

USBODE uses a C++ bare-metal environment based on the [Circle](https://github.com/rsta2/circle) framework.

*   **Language**: C++ (standard C++11/14 features supported, but no exceptions or RTTI).
*   **Indentation**: 4 spaces.
*   **Naming Conventions**:
    *   Classes: `CClassName` (e.g., `CKernel`, `CWebServer`).
    *   Member variables: `m_VariableName` (e.g., `m_Screen`).
    *   Global variables: `g_VariableName`.
    *   Functions/Methods: `MethodName` (CamelCase).
*   **Strings**: Use `CString` from Circle or standard C strings (`char*`). Prefer safe functions like `snprintf` over `strcpy`/`sprintf`.
*   **Logging**: Use the macros `LOGNOTE`, `LOGWARN`, `LOGERR`, `LOGDBG`.

## Code Structure

The repository is organized as follows:

*   `src/`: Contains the main kernel entry point (`kernel.cpp`, `main.cpp`) and core initialization logic.
*   `addon/`: Contains modular components (addons) that provide specific functionality.
*   `circle-stdlib/`: Standard library support for the Circle framework.
*   `tools/`: Helper scripts and tools.

### Core Initialization
The `CKernel` class in `src/kernel.cpp` is responsible for initializing the hardware and starting the various services (addons).

## Addons System

The project uses a modular "addon" system to organize functionality. Each addon is located in the `addon/` directory.

### Directory Structure

Each addon typically contains:
*   Header files (`.h`)
*   Source files (`.cpp`)
*   A `Makefile`

### Creating a New Addon

1.  **Create Directory**: Create a new directory in `addon/` (e.g., `addon/myfeature/`).
2.  **Add Code**: Add your C++ classes and logic.
3.  **Create Makefile**: Create a `Makefile` that compiles your sources into a static library (e.g., `libmyfeature.a`).
    *   Look at existing addons (e.g., `addon/webserver/Makefile`) for examples.

### Registering the Addon

To make the build system aware of your new addon, you must register it in two places:

1.  **Root Makefile (`Makefile`)**:
    Add your addon directory name to the `USBODE_ADDONS` list.
    ```makefile
    USBODE_ADDONS = ... myfeature
    ```

2.  **Source Makefile (`src/Makefile`)**:
    Add the library to the `LIBS` list so it gets linked into the kernel.
    ```makefile
    LIBS = ... ../addon/myfeature/libmyfeature.a
    ```

## Disc Image Plugins

USBODE supports various CD image formats through a plugin architecture rooted in the `IImageDevice` interface.

### Architecture

*   **Interface**: `IImageDevice` (in `addon/discimage/imagedevice.h`) defines the standard operations for a disc image (Seek, Read, GetCueSheet, etc.).
*   **Factory**: `addon/discimage/util.cpp` contains the logic to detect file types and instantiate the correct device class.
*   **File Types**: `FileType` enum in `addon/discimage/filetype.h`.

### Adding Support for a New Format

To add support for a new image format (e.g., `.ccd`):

1.  **Create Device Class**: Create a new class (e.g., `CCcdFileDevice`) in `addon/discimage/` that inherits from `IImageDevice`.
    *   Implement all pure virtual methods.
    *   If the format uses a CUE sheet concept, you can generate it in memory and return it via `GetCueSheet()`.

2.  **Update FileType Enum**: Add a new entry to the `FileType` enum in `addon/discimage/filetype.h`.
    ```cpp
    enum FileType {
        ...
        FormatCcd
    };
    ```

3.  **Register in Factory**: Update `GetImageDevice` in `addon/discimage/util.cpp`.
    *   Add logic to detect the file extension (e.g., `.ccd`).
    *   Instantiate your new class.

4.  **Update Build**: Add your new `.cpp` file to `addon/discimage/Makefile` so it gets compiled.

### Subchannel Data

If your format supports subchannel data (e.g., for copy protection), implement:
*   `HasSubchannelData()`: Return `true`.
*   `ReadSubchannel(u32 lba, u8* buffer)`: Fill the buffer with subchannel data for the given LBA.

## Web Interface

The USBODE web interface provides a user-friendly way to manage the device, select images, and configure settings. It is implemented in C++ using the Circle framework and uses `mustache` for HTML templating.

### Architecture

The web server is located in `addon/webserver/`. The core components are:

*   **`CWebServer`**: Extends `CHTTPDaemon`. It handles incoming HTTP connections and routes requests.
*   **`PageHandlerRegistry`**: A central registry that maps URL paths (e.g., `/`, `/config`) to specific handler instances.
*   **`PageHandlerBase`**: A base class for all page handlers. It handles the `mustache` template rendering and provides a common structure.
*   **`IPageHandler`**: The interface that all handlers must implement.

### Templating System

USBODE uses [mustache](https://mustache.github.io/) for logic-less templates.

*   Templates are located in `addon/webserver/pages/`.
*   During the build, `tools/converttool` converts these `.html` files into `.h` header files containing C strings.
*   The `PageHandlerBase` class loads the global template (`s_Template`) and renders it with a context provided by the specific page handler.

### Theming System

The visual style of the web interface is defined by a centralized CSS file.

*   **Location**: `addon/webserver/assets/style.css`
*   **Mechanism**: The CSS file is embedded directly into the binary as a static asset.
*   **Modifying the Theme**: To change the look and feel (colors, fonts, layout), you must edit `style.css` and recompile the project. The build system automatically handles the conversion of the CSS file into a C header.

### Adding a New Page

To add a new page to the web interface:

1.  **Create Template**: Create a new `.html` file in `addon/webserver/pages/` (e.g., `mypage.html`).
2.  **Create Handler**: Create a new class in `addon/webserver/handlers/` that inherits from `PageHandlerBase`.
    *   Implement `GetHTML()` to return the content of your template.
    *   Implement `PopulateContext()` to add dynamic data to the Mustache context.
3.  **Register Handler**: Add your new handler to `addon/webserver/pagehandlerregistry.cpp`.
    *   Instantiate your handler (e.g., `static MyPageHandler s_myPageHandler;`).
    *   Add a route to the `g_pageHandlers` map.
4.  **Update Makefile**: Add your new handler's object file to `OBJS` in `addon/webserver/Makefile`.

## Display Service

The Display Service manages the on-device screen (OLED/LCD) and physical buttons. It allows the user to navigate menus directly on the device.

### Architecture

The code is located in `addon/displayservice/`.

*   **`DisplayService`**: The main service class (extends `CTask`). It initializes the specific display hardware based on the configuration.
*   **`IDisplay`**: An interface that abstracts the underlying hardware. Implementations include `ST7789Display` and `CSH1106Display`.
*   **`PageManager`**: Manages the stack of UI pages and handles navigation.
*   **`IPage`**: The interface for a UI page (e.g., Home Page, Config Page).
*   **`CGPIOManager`**: Handles GPIO interrupts for button presses.

### Adding Support for New Hardware

To support a new display:

1.  **Create Directory**: Create a new directory in `addon/displayservice/` (e.g., `mydisplay/`).
2.  **Implement IDisplay**: Create a class that implements `IDisplay`.
3.  **Implement Pages**: You may need to implement specific versions of pages if the resolution or capabilities differ significantly (e.g., color vs. monochrome).
4.  **Register**: Update `DisplayService::CreateDisplay` in `addon/displayservice/displayservice.cpp` to instantiate your class based on the `displayhat` config setting.

### Creating a New UI Page

1.  **Create Class**: Create a class that inherits from `IPage`.
2.  **Implement Methods**:
    *   `Draw()`: Render the UI.
    *   `OnButtonPress(Button button)`: Handle input.
    *   `OnEnter()`, `OnExit()`: Lifecycle hooks.
3.  **Register**: Register your page in the `Initialize` method of the specific Display implementation (e.g., `ST7789Display::Initialize`).

## Submitting Changes

1.  Fork the repository.
2.  Create a new branch for your feature or fix.
3.  Make your changes.
4.  Ensure the code compiles for all supported architectures (see `BUILD.md`).
5.  Submit a Pull Request.

Please ensure your code follows the existing style and conventions.
