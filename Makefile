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
# --- NVIDIA Driver Version Override (Used for both build and installation path) ---
# Allows specifying an NVIDIA driver version. This will be embedded into the shim library
# and used to construct the installation path.
#
# Example usage:
#   make install NVIDIA_DRIVER_VERSION=470.82.01
#
# If NVIDIA_DRIVER_VERSION is not provided, it defaults to the version below.
# The `?=` operator sets the variable only if it's not already set from the command line.
NVIDIA_DRIVER_VERSION ?= 535.104.05

# Destination for the kernel module. Using 'extra' is a common practice.
KMOD_INSTALL_PATH := /lib/modules/$(KVERSION)/kernel/drivers/extra

# --- OPTIMIZED: Multi-layered library path detection ---
# This robust logic determines the correct library installation directory.
# It allows user override as the highest priority.
#
# To manually specify the library path, run:
#   make install LIBDIR=/path/to/your/libs

# Layer 1: Check if the user has manually specified LIBDIR.
ifeq ($(LIBDIR),)
  # Layer 2: Query the compiler for its 'multi-os-directory'. This is the preferred method.
  # - On Debian/Ubuntu (x86_64), returns: ../lib/x86_64-linux-gnu
  # - On CentOS/RHEL (x86_64), returns: .
  # - On Debian (aarch64), returns: ../lib/aarch64-linux-gnu
  # This is architecture-aware and distribution-aware.
  MULTI_OS_DIR_RAW := $(strip $(shell $(CC) -print-multi-os-directory 2>/dev/null))

  # Case A: Compiler returns '.' (typical for CentOS/RHEL).
  ifeq ($(MULTI_OS_DIR_RAW),.)
    SHIM_INSTALL_DIR := /usr/lib64
  # Case B: Compiler returns a relative path (typical for Debian/Ubuntu).
  else ifneq ($(MULTI_OS_DIR_RAW),)
    # The output is like '../lib/x86_64-linux-gnu'. We construct the absolute path.
    # Using patsubst is a pure-make way to remove the leading '../'.
    SHIM_INSTALL_DIR := /usr/$(patsubst ../,%,$(MULTI_OS_DIR_RAW))
  # Case C: Compiler query failed or returned nothing. Use fallback logic.
  else
    # Layer 3: Fallback to checking well-known directory paths.
    ifneq ($(shell test -d /usr/lib/x86_64-linux-gnu && echo YES),)
      SHIM_INSTALL_DIR := /usr/lib/x86_64-linux-gnu
    else ifneq ($(shell test -d /usr/lib64 && echo YES),)
      SHIM_INSTALL_DIR := /usr/lib64
    else
      # Layer 4: Final fallback to a generic path.
      SHIM_INSTALL_DIR := /usr/lib
    endif
  endif
else
  # Use the user-provided directory.
  SHIM_INSTALL_DIR := $(LIBDIR)
endif


# The full path for the versioned library file.
SHIM_INSTALL_PATH_VERSIONED := $(SHIM_INSTALL_DIR)/libnvidia-ml.so.$(NVIDIA_DRIVER_VERSION)
# The path for the primary symlink (.so.1).
SHIM_SYMLINK_1 := $(SHIM_INSTALL_DIR)/libnvidia-ml.so.1
# The path for the secondary symlink (.so).
SHIM_SYMLINK_0 := $(SHIM_INSTALL_DIR)/libnvidia-ml.so

# Paths for the fake device service.
DEVICE_INSTALL_PATH := /usr/local/bin/fake-nvidia-device.sh
SERVICE_FILE_PATH := /etc/systemd/system/fake-nvidia-device.service


# --- Part 4: Build Rules ---

# 'all' is the default target, which is executed when 'make' is run.
# It depends on the kernel module and the shared library.
.PHONY: all
all: kernel_module $(SHIM_TARGET)
	@echo "Build complete for kernel version $(KVERSION). Products:"
	@echo "  - Kernel Module: fake_nvidia_driver.ko"
	@echo "  - LD_PRELOAD Shim: $(SHIM_TARGET)"
	@echo "Detected library installation directory: $(SHIM_INSTALL_DIR)"

# Rule for building the kernel module.
.PHONY: kernel_module
kernel_module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Rule for building the shared library.
# Replaces the driver version string in the source file before compilation.
# $@ represents the target file (libfake_nvml.so).
# $^ represents all dependency files (fake_nvml.c).
$(SHIM_TARGET): $(SHIM_SOURCE)
	@echo "Using NVIDIA driver version $(NVIDIA_DRIVER_VERSION) for build..."
	# Create a temporary copy to avoid modifying the original source file if it's under version control.
	cp $(SHIM_SOURCE) $(SHIM_SOURCE).tmp.c
	# Replace the default version string with the specified NVIDIA_DRIVER_VERSION.
	sed -i 's/535.104.05/$(NVIDIA_DRIVER_VERSION)/g' $(SHIM_SOURCE).tmp.c
	# Compile using the temporary, modified source file.
	$(CC) $(SHIM_CFLAGS) -o $@ $(SHIM_SOURCE).tmp.c
	# Remove the temporary file.
	rm $(SHIM_SOURCE).tmp.c


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
	# --- Kernel Module Installation ---
	mkdir -p $(KMOD_INSTALL_PATH)
	install -m 644 fake_nvidia_driver.ko $(KMOD_INSTALL_PATH)/
	echo "fake_nvidia_driver" > /etc/modules-load.d/fake_nvidia_driver.conf
	depmod -a || true

	# --- Shim Library Installation ---
	@echo "Installing shim library to $(SHIM_INSTALL_PATH_VERSIONED)..."
	# Copy the shared library to the system directory with the versioned name.
	install -m 755 $(SHIM_TARGET) $(SHIM_INSTALL_PATH_VERSIONED)
	# Create the symbolic links. Use -f to overwrite existing links.
	@echo "Creating symbolic links..."
	ln -sf $(SHIM_INSTALL_PATH_VERSIONED) $(SHIM_SYMLINK_1)
	ln -sf $(SHIM_SYMLINK_1) $(SHIM_SYMLINK_0)
	# Update the dynamic linker's cache.
	ldconfig

	# --- Service Installation ---
	install -m 755 fake-nvidia-device.sh $(DEVICE_INSTALL_PATH)
	install -m 644 fake-nvidia-device.service $(SERVICE_FILE_PATH)
	systemctl enable fake-nvidia-device.service
	@echo "Installation complete."
	@echo "Run 'sudo systemctl start fake-nvidia-device.service' to start the service."

# 'uninstall' target to remove files from system directories.
.PHONY: uninstall
uninstall:
	@echo "Uninstalling kernel module, shim library and service..."
	# --- Service Uninstallation ---
	systemctl stop fake-nvidia-device.service || true
	systemctl disable fake-nvidia-device.service || true
	rm -f $(SERVICE_FILE_PATH)
	rm -f $(DEVICE_INSTALL_PATH)
	systemctl daemon-reload

	# --- Kernel Module Uninstallation ---
	rm -f $(KMOD_INSTALL_PATH)/fake_nvidia_driver.ko
	rm -f /etc/modules-load.d/fake_nvidia_driver.conf
	depmod -a || true

	# --- Shim Library Uninstallation ---
	@echo "Removing shim library and symbolic links..."
	# Remove the versioned library file and both symbolic links.
	rm -f $(SHIM_INSTALL_PATH_VERSIONED)
	rm -f $(SHIM_SYMLINK_1)
	rm -f $(SHIM_SYMLINK_0)
	# Update the dynamic linker's cache.
	ldconfig
	@echo "Uninstallation complete."
