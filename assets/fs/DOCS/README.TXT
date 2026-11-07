BORDIFY OS
==========

Bordify is a 32-bit x86 operating system built from scratch.

It boots with Multiboot2 and GRUB, switches to a linear framebuffer and
draws its own window manager. There is no Linux underneath and no web
browser host process.

STORAGE
-------
The first ATA disk is accessed through programmed I/O. The boot image
contains a FAT16 partition with the kernel, these documents, the Web
pages and the settings file.

INPUT
-----
The PS/2 controller provides keyboard and mouse input. The PIT supplies
the system clock. Window dragging, text editing and scrolling all use
these devices directly.

APPS
----
Files  browse, create, rename, read and delete files
web    read the HTML pages in WEB/
Settings  theme, wallpaper, accent, clock and power

SOURCE
------
The complete source is in the public repository that built this image.
