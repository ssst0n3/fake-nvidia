# Makefile for building both the Kernel Module and the LD_PRELOAD Shim

# --- Preamble: Kernel Version Configuration ---
# Allows specifying a target kernel version via the KERNEL_RELEASE variable on the command line.
# This is useful for building inside a container where `uname -r` might reflect the host's kernel
# instead of the target kernel for which the headers are installed.
#
# Example usage:
#   make KERNEL_RELEASE=5.15.0-101-generic
#
# If KERNEL_RELEASE is not provided, it defaults to the currently running kernel version.
ifeq ($(KERNEL_RELEASE),)
  KVERSION := $(shell uname -r)
else
  KVERSION := $(KERNEL_RELEASE)
endif

# --- Part 1: Kernel Module Configuration ---
# 'obj-m' tells the kernel build system that we want to build a module.
obj-m += fake_nvidia_driver.o

# Path to the kernel source/header files, now using the configurable KVERSION.
KDIR := /lib/modules/$(KVERSION)/build


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
KMOD_INSTALL_PATH := /lib/modules/$(KVERSION)/kernel/drivers/extra
# Destination for the shared library. /usr/local/lib is a standard location.
SHIM_INSTALL_PATH := /usr/local/lib/libnvidia-ml.so.1
MKNOD_INSTALL_PATH := /usr/local/bin/fake-nvidia-mknod.sh
SERVICE_FILE_PATH := /etc/systemd/system/fake-nvidia-mknod.service


# --- Part 4: Build Rules ---

# --- NVIDIA Driver Version Override (Optional) ---
# Allows specifying an NVIDIA driver version to be embedded into the shim library.
# This will replace the default version string in fake_nvml.c before compilation.
#
# Example usage:
#   make NVIDIA_DRIVER_VERSION=470.82.01
#
# If NVIDIA_DRIVER_VERSION is not provided, the default version in the source file is used.
# Note: This modifies the source file 'fake_nvml.c' in-place.

# 'all' is the default target, which is executed when 'make' is run.
# It depends on the kernel module and the shared library.
.PHONY: all
all: kernel_module $(SHIM_TARGET)
	@echo "Build complete for kernel version $(KVERSION). Products:"
	@echo "  - Kernel Module: fake_nvidia_driver.ko"
	@echo "  - LD_PRELOAD Shim: $(SHIM_TARGET)"

# Rule for building the kernel module.
.PHONY: kernel_module
kernel_module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Rule for building the shared library.
# Now includes logic to optionally replace the driver version string.
# $@ represents the target file (libfake_nvml.so).
# $^ represents all dependency files (fake_nvml.c).
$(SHIM_TARGET): $(SHIM_SOURCE)
	@if [ -n "$(NVIDIA_DRIVER_VERSION)" ]; then \
		echo "Overriding NVIDIA driver version to $(NVIDIA_DRIVER_VERSION) in $(SHIM_SOURCE)..."; \
		sed -i 's/535.104.05/$(NVIDIA_DRIVER_VERSION)/g' $(SHIM_SOURCE); \
	fi
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
	# Auto load the module on boot.
	echo "fake_nvidia_driver" > /etc/modules-load.d/fake_nvidia_driver.conf
	# Copy the shared library to the system directory with proper permissions.
	install -m 755 $(SHIM_TARGET) $(SHIM_INSTALL_PATH)
	# Update the dynamic linker's cache.
	ldconfig
	# Install the mknod service.
	install -m 755 mknod.sh $(MKNOD_INSTALL_PATH)
	install -m 644 mknod.service $(SERVICE_FILE_PATH)
	systemctl enable fake-nvidia-mknod.service
	@echo "Installation complete."

# 'uninstall' target to remove files from system directories.
.PHONY: uninstall
uninstall:
	@echo "Uninstalling kernel module, shim library and service..."
	# Stop and disable the mknod service.
	systemctl stop fake-nvidia-mknod.service || true
	systemctl disable fake-nvidia-mknod.service || true
	# Remove the mknod service and script.
	rm -f $(SERVICE_FILE_PATH)
	rm -f $(MKNOD_INSTALL_PATH)
	# Reload the systemd daemon to apply changes.
	systemctl daemon-reload
	# Remove the kernel module from the system directory.
	rm -f $(KMOD_INSTALL_PATH)/fake_nvidia_driver.ko
	# Remove the auto-load configuration for the kernel module.
	rm -f /etc/modules-load.d/fake_nvidia_driver.conf
	# Update the list of module dependencies.
	depmod -a
	# Remove the shared library from the system directory.
	rm -f $(SHIM_INSTALL_PATH)
	# Update the dynamic linker's cache.
	ldconfig
	@echo "Uninstallation complete."
