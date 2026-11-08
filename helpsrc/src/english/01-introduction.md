# Introduction

`TinyDev` is an advanced editor intended primarily for programmers. It was
developed around the `TextEdit` gadget of AmigaOS 3.2 and therefore requires
at least AmigaOS 3.2. It does not work on older versions of the system.

Among other things, `TinyDev` offers the following features:

- ReAction-based user interface with support for translations  
(English built in; German catalogs are included)
- Integrated directory browser
- Minimap
- Simple project management
- File and project templates
- Syntax highlighting for various languages
- Bookmark management
- Static source code analysis
- Symbol list
- Auto-completion based on the symbols found  
(**not** a simple keyword list)
- Find and replace (Amiga+F) in multiple documents; supports regular expressions
- Integrated documentation (work in progress)
    - The `HelpViewer` is a simple but functional tool for displaying the
      supplied documentation
    - `TinyDev` includes a project template and all the tools needed to
      create and edit your own help pages
- Extensibility without changing `TinyDev`:
    - You can add your own templates
    - You can add your own source code parsers
- Reusable components:
    - The ReAction classes `dirtree` and `minimap` can be used in your own
      ReAction applications
    - The source code analysis tools (Scout) are designed so that they can
      be integrated into your own applications through a simple JSON-based
      protocol
    - The `HelpViewer` is designed as a general-purpose tool. Its license
      allows it to be embedded in other projects


## Requirements

`TinyDev` requires AmigaOS 3.2. It does not work on older versions of the
system. It has only been tested on AmigaOS 3.2.3.

The `HelpViewer` needs the ReAction class `htmlgadget`. It is
[available](http://aminet.net/package/dev/gui/html_gadget) on Aminet or from
its [GitHub page](https://github.com/andregewert/Amiga-HTML-Gadget).

To open Internet links from the `HelpViewer`, you need
[OpenURL](http://aminet.net/package/comm/www/OpenURL-7.18).


## Installation

An installation script is provided. All required components are installed
into the target drawer. Optionally, a copy of the Markdown converter
`mdtohtml` can be copied to `c:`.

Catalogs, gadgets and parsers are loaded from the application drawer and do
not have to be copied to the system partition. The commodity for source code
analysis (`TinyDev-Scout`) does not have to be copied to `WBStartup` either.
Starting it automatically would hardly be useful anyway: `TinyDev` starts the
commodity itself when needed (from the application drawer).
