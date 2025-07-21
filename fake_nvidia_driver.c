#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

// We just need a pointer to the root of the directory we create.
static struct proc_dir_entry *g_proc_nvidia_dir = NULL;

// Fake version information.
static const char g_fake_version_string[] = "Driver Version: 535.104.05\n";

// This function is called when /proc/driver/nvidia/version is read.
static ssize_t proc_version_read(struct file *file, char __user *usr_buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(usr_buf, count, ppos, g_fake_version_string, sizeof(g_fake_version_string));
}

// Bind the read operation to the function.
static const struct proc_ops g_version_fops = {
    .proc_read = proc_version_read,
};

// Module initialization function.
static int __init fake_nvidia_init(void) {
    struct proc_dir_entry *gpus_dir;

    printk(KERN_INFO "FAKE_NVIDIA: Loading Fake NVIDIA Driver Module (v6 - Correct GPU Path)...\n");

    // Create the /proc/driver/nvidia directory directly using the path.
    g_proc_nvidia_dir = proc_mkdir("driver/nvidia", NULL);
    if (!g_proc_nvidia_dir) {
        printk(KERN_ERR "FAKE_NVIDIA: Failed to create directory /proc/driver/nvidia.\n");
        return -ENOMEM;
    }

    // Create files and subdirectories under it.
    proc_create("version", 0444, g_proc_nvidia_dir, &g_version_fops);
    gpus_dir = proc_mkdir("gpus", g_proc_nvidia_dir);
    
    if (gpus_dir) {
        // *** This is the only modification ***
        // According to the error log, the program needs '0000:00:00.0'.
        proc_mkdir("0000:00:00.0", gpus_dir);
    }

    printk(KERN_INFO "FAKE_NVIDIA: Module loaded and /proc/driver/nvidia structure created successfully.\n");
    return 0;
}

// Module exit function.
static void __exit fake_nvidia_exit(void) {
    printk(KERN_INFO "FAKE_NVIDIA: Unloading Fake NVIDIA Driver Module (v6)...\n");
    
    if (g_proc_nvidia_dir) {
        // proc_remove is recursive, so it will clean up subdirectories and files.
        proc_remove(g_proc_nvidia_dir);
    }
    
    printk(KERN_INFO "FAKE_NVIDIA: Cleanup complete.\n");
}

module_init(fake_nvidia_init);
module_exit(fake_nvidia_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("ssst0n3 with gemini-2.5-pro");
MODULE_DESCRIPTION("A fake driver with the correct GPU PCI path for nvidia-container-cli.");