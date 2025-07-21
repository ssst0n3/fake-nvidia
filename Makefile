# Makefile for building both the Kernel Module and the LD_PRELOAD Shim

# --- Part 1: Kernel Module Configuration ---
# 'obj-m' tells the kernel build system that we want to build a module.
obj-m += fake_nvidia_driver.o

# Path to the kernel source/header files.
KDIR := /lib/modules/$(shell uname -r)/build


# --- Part 2: LD_PRELOAD Shim Configuration ---
# Filename for the target shared library.
SHIM_TARGET := libfake_nvml.so
# Source filename.
SHIM_SOURCE := fake_nvml.c
# C compiler.
CC := gcc
# Special flags required for compiling a shared library.
# -shared: Generate a shared library.
# -fPIC:   Generate Position-Independent Code, a requirement for shared libraries.
# -ldl:    Link against the dynamic linking library (not used in this version, but good practice).
SHIM_CFLAGS := -shared -fPIC -ldl


# --- Part 3: Installation Path Configuration ---
# Destination for the kernel module. Using 'extra' is a common practice.
KMOD_INSTALL_PATH := /lib/modules/$(shell uname -r)/kernel/drivers/extra
# Destination for the shared library. /usr/local/lib is a standard location.
SHIM_INSTALL_PATH := /usr/local/lib/libnvidia-ml.so.1


# --- Part 4: Build Rules ---

# 'all' is the default target, which is executed when 'make' is run.
# It depends on the kernel module and the shared library.
.PHONY: all
all: kernel_module $(SHIM_TARGET)
	@echo "Build complete. Products:"
	@echo "  - Kernel Module: fake_nvidia_driver.ko"
	@echo "  - LD_PRELOAD Shim: $(SHIM_TARGET)"

# Rule for building the kernel module.
.PHONY: kernel_module
kernel_module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Rule for building the shared library.
# $@ represents the target file (libfake_nvml.so).
# $^ represents all dependency files (fake_nvml.c).
$(SHIM_TARGET): $(SHIM_SOURCE)
	$(CC) $(SHIM_CFLAGS) -o $@ $^

# 'clean' target is used to delete all generated files.
.PHONY: clean
clean:
	# Clean up the build files for the kernel module.
	@echo "Cleaning kernel module artifacts..."
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	# Clean up our own shared library file.
	@echo "Cleaning shim library..."
	rm -f $(SHIM_TARGET)


# --- Part 5: Install and Uninstall Rules ---

# 'install' target to copy files to system directories.
.PHONY: install
install: all
	@echo "Installing kernel module and shim library..."
	# Create the destination directory for the kernel module if it doesn't exist.
	mkdir -p $(KMOD_INSTALL_PATH)
	# Copy the kernel module to the system directory with proper permissions.
	install -m 644 fake_nvidia_driver.ko $(KMOD_INSTALL_PATH)/
	# Update the list of module dependencies.
	depmod -a
	# Copy the shared library to the system directory with proper permissions.
	install -m 755 $(SHIM_TARGET) $(SHIM_INSTALL_PATH)
	# Update the dynamic linker's cache.
	ldconfig
	@echo "Installation complete."

# 'uninstall' target to remove files from system directories.
.PHONY: uninstall
uninstall:
	@echo "Uninstalling kernel module and shim library..."
	# Remove the kernel module from the system directory.
	rm -f $(KMOD_INSTALL_PATH)/fake_nvidia_driver.ko
	# Update the list of module dependencies.
	depmod -a
	modprobe fake_nvidia_driver
	# Remove the shared library from the system directory.
	rm -f $(SHIM_INSTALL_PATH)
	# Update the dynamic linker's cache.
	ldconfig
	@echo "Uninstallation complete."