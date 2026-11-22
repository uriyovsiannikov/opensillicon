#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/cyclone_net.h"
#include "kernel/process.h"
#include "kernel/module.h"
#include "kernel/pmm.h"
#include "kernel/procfs.h"
#include "kernel/timer.h"
#include "kernel/block.h"
#include "kernel/smp.h"
#include "kernel/usb_host.h"
#include "kernel/driver_api.h"
#include "kernel/hda_probe.h"
#include "arch/x86_64/pci.h"

#define PROCFS_CONTENT_MAX 16384U
#define PROCFS_TICK_HZ 100U

enum procfs_entry {
	PROCFS_ROOT,
	PROCFS_SELF,
	PROCFS_UPTIME,
	PROCFS_MEMINFO,
	PROCFS_CPUINFO,
	PROCFS_MODULES,
	PROCFS_PCI,
	PROCFS_USB,
	PROCFS_DISKS,
	PROCFS_CMDLINE,
	PROCFS_NETPCI,
	PROCFS_AUDIOPCI,
	PROCFS_HDACODECS,
	PROCFS_NETIF,
	PROCFS_DAPI
};

struct procfs_node {
	struct vnode vnode;
	enum procfs_entry entry;
	size_t content_length;
	uint64_t self_inode;
	uint64_t uptime_inode;
	uint64_t meminfo_inode;
	uint64_t cpuinfo_inode;
	uint64_t modules_inode;
	uint64_t pci_inode, usb_inode, disks_inode, cmdline_inode, netpci_inode,
	    netif_inode, dapi_inode, audiopci_inode, hdacodecs_inode, hdatest_inode;
	char name[16];
	char content[PROCFS_CONTENT_MAX];
};

static char boot_cmdline[1024];
void
procfs_set_cmdline(const char *cmdline)
{
	size_t i = 0;
	while (cmdline && cmdline[i] && i < sizeof(boot_cmdline) - 1) {
		boot_cmdline[i] = cmdline[i];
		i++;
	}
	boot_cmdline[i] = 0;
}

static int procfs_lookup(struct vnode *, const char *, size_t, struct vnode **);
static int procfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int procfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void procfs_destroy(struct vnode *);
static struct procfs_node *procfs_allocate(const char *, enum procfs_entry,
	enum vnode_type, uint64_t);

static const struct vnode_operations procfs_operations = {
	.lookup = procfs_lookup,
	.read = procfs_read,
	.readdir = procfs_readdir,
	.destroy = procfs_destroy
};

static struct procfs_node *
procfs_from_vnode(struct vnode *vnode)
{
	return (struct procfs_node *)vnode->private_data;
}

static bool
append_text(struct procfs_node *node, const char *text)
{
	size_t length = strlen(text);

	if (length > sizeof(node->content) - node->content_length)
		return false;
	memcpy(node->content + node->content_length, text, length);
	node->content_length += length;
	return true;
}

static bool
append_u64(struct procfs_node *node, uint64_t value)
{
	char digits[20];
	size_t length = 0;

	do {
		digits[length++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0);
	if (length > sizeof(node->content) - node->content_length)
		return false;
	while (length != 0)
		node->content[node->content_length++] = digits[--length];
	return true;
}

static bool
append_hex(struct procfs_node *node, uint64_t value, unsigned int digits)
{
	static const char hex[] = "0123456789abcdef";
	if (digits > sizeof(node->content) - node->content_length)
		return false;
	for (unsigned int i = digits; i != 0; i--)
		node->content[node->content_length++] = hex[(value >> ((i - 1U) * 4U)) & 15U];
	return true;
}

static bool
procfs_pci_identity(const struct pci_device_info *device, struct procfs_node *node)
{
	return append_hex(node, device->segment, 4) && append_text(node, ":") &&
	    append_hex(node, device->bus, 2) && append_text(node, ":") &&
	    append_hex(node, device->device, 2) && append_text(node, ".") &&
	    append_u64(node, device->function) && append_text(node, " class=") &&
	    append_hex(node, device->class_code, 2) && append_hex(node, device->subclass, 2) &&
	    append_hex(node, device->programming_interface, 2) && append_text(node, " ") &&
	    append_hex(node, device->vendor_id, 4) && append_text(node, " ") &&
	    append_hex(node, device->device_id, 4);
}

static bool
procfs_pci_entry(const struct pci_device_info *device, void *context)
{
	struct procfs_node *node = context;
	return procfs_pci_identity(device, node) && append_text(node, "\n");
}

static bool
append_config_id(struct procfs_node *node, const struct pci_device_info *device,
    uint16_t offset, unsigned int bits)
{
	uint32_t value;
	if (!pci_config_read(device->segment, device->bus, device->device,
	    device->function, offset, bits, &value))
		return append_text(node, "unavailable");
	return append_hex(node, value, bits / 4U);
}

static bool
procfs_device_properties(const struct pci_device_info *device, struct procfs_node *node,
    const char *description)
{
	if (!procfs_pci_identity(device, node) ||
	    !append_text(node, description) ||
	    !append_text(node, "  revision=") ||
	    !append_config_id(node, device, 0x08, 8) ||
	    !append_text(node, " command=") ||
	    !append_hex(node, device->command, 4))
		return false;
	/* Subsystem fields and these interrupt offsets apply to type-0 headers. */
	if ((device->header_type & 0x7fU) == 0 &&
	    (!append_text(node, " subsystem=") ||
	    !append_config_id(node, device, 0x2c, 16) ||
	    !append_text(node, ":") ||
	    !append_config_id(node, device, 0x2e, 16) ||
	    !append_text(node, " irq-line=") ||
	    !append_config_id(node, device, 0x3c, 8) ||
	    !append_text(node, " irq-pin=") ||
	    !append_config_id(node, device, 0x3d, 8)))
		return false;
	if (!append_text(node, "\n"))
		return false;
	for (unsigned int i = 0; i < 6; i++) {
		struct pci_bar_resource bar;
		if (!pci_bar_decode(device, i, &bar) || bar.type == PCI_BAR_UNUSED)
			continue;
		const char *type = bar.type == PCI_BAR_IO ? " IO 0x" :
		    bar.type == PCI_BAR_MEMORY64 ? " MMIO64 0x" : " MMIO32 0x";
		if (!append_text(node, "  BAR") || !append_u64(node, i) ||
		    !append_text(node, type) ||
		    !append_hex(node, bar.base, bar.type == PCI_BAR_MEMORY64 ? 16 : 8) ||
		    !append_text(node, "\n"))
			return false;
		i += bar.slots - 1;
	}
	return true;
}

static bool
procfs_netpci_entry(const struct pci_device_info *device, void *context)
{
    if (device->class_code != 0x02) return true;
    return procfs_device_properties(device, context,
        device->subclass == 0 ? " Ethernet\n" : " Network controller\n");
}

static bool
procfs_audiopci_entry(const struct pci_device_info *device, void *context)
{
    struct procfs_node *node = context;
    if (device->class_code != 0x04) return true;
    const char *type = device->subclass == 0x03 ? " High Definition Audio\n" :
        device->subclass == 0x01 ? " Audio controller (AC97/legacy)\n" :
        device->subclass == 0x00 ? " Multimedia video function\n" :
        " Multimedia function\n";
    if (!procfs_device_properties(device, node, type) ||
        !append_text(node, "  status=") || !append_config_id(node, device, 0x06, 16) ||
        !append_text(node, " header=") || !append_hex(node, device->header_type, 2) ||
        !append_text(node, "\n  capabilities:")) return false;
    uint32_t status, next;
    if (pci_config_read(device->segment, device->bus, device->device,
        device->function, 0x06, 16, &status) && (status & 0x10) &&
        (device->header_type & 0x7fU) == 0 &&
        pci_config_read(device->segment, device->bus, device->device,
            device->function, 0x34, 8, &next)) {
        uint64_t seen = 0;
        for (unsigned int count = 0; next != 0 && count < 48; ++count) {
            if (next < 0x40 || next > 0xfc || (next & 3) ||
                (seen & (UINT64_C(1) << (next / 4)))) {
                if (!append_text(node, " invalid-chain")) return false;
                break;
            }
            seen |= UINT64_C(1) << (next / 4);
            uint32_t cap;
            if (!pci_config_read(device->segment, device->bus, device->device,
                device->function, (uint16_t)next, 16, &cap)) break;
            const char *name = (cap & 0xff) == 1 ? "PM" :
                (cap & 0xff) == 5 ? "MSI" : (cap & 0xff) == 0x11 ? "MSI-X" :
                (cap & 0xff) == 0x10 ? "PCIe" : "id";
            if (!append_text(node, " ") || !append_text(node, name) ||
                !append_text(node, "=") || !append_hex(node, cap & 0xff, 2) ||
                !append_text(node, "@") || !append_hex(node, next, 2)) return false;
            next = (cap >> 8) & 0xff;
        }
    }
    if (!append_text(node, "\n  PCI config (offset: 32-bit words):\n")) return false;
    for (unsigned int offset = 0; offset < 256; offset += 32) {
        if (!append_text(node, "  ") || !append_hex(node, offset, 2) || !append_text(node, ":")) return false;
        for (unsigned int i = 0; i < 32; i += 4)
            if (!append_text(node, " ") || !append_config_id(node, device, offset+i, 32)) return false;
        if (!append_text(node, "\n")) return false;
    }
    return append_text(node, "\n");
}

static int
procfs_make_audiopci(uint64_t inode, struct procfs_node **result)
{
    struct procfs_node *node = procfs_allocate("audiopci", PROCFS_AUDIOPCI, VNODE_REGULAR, inode);
    if (!node) return -ENOMEM;
    if (!append_text(node, "Audio PCI diagnostic v1 (read-only)\n") ||
        !pci_enumerate(procfs_audiopci_entry, node)) {
        vnode_put(&node->vnode); return -EINVAL;
    }
    if (node->content_length == strlen("Audio PCI diagnostic v1 (read-only)\n"))
        (void)append_text(node, "No PCI multimedia functions found. For USB audio also run usbconf.\n");
    node->vnode.size = node->content_length;
    *result = node;
    return 0;
}

static int
procfs_make_pci(uint64_t inode, struct procfs_node **result, bool network)
{
	struct procfs_node *node = procfs_allocate(network ? "netpci" : "pci",
	    network ? PROCFS_NETPCI : PROCFS_PCI, VNODE_REGULAR, inode);
	if (node == NULL) return -ENOMEM;
	if (!pci_enumerate(network ? procfs_netpci_entry : procfs_pci_entry, node)) {
		vnode_put(&node->vnode); return -EINVAL;
	}
	node->vnode.size = node->content_length; *result = node; return 0;
}

static int
procfs_make_usb(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("usb", PROCFS_USB, VNODE_REGULAR, inode);
	struct usb_host_device_info devices[USB_HOST_DEVICE_MAX];
	if (node == NULL) return -ENOMEM;
	if (!append_text(node, "controllers: ") || !append_u64(node, usb_host_controller_count()) ||
	    !append_text(node, "\nactive: ") || !append_u64(node, usb_host_available()) ||
	    !append_text(node, "\n")) goto overflow;
	size_t count = usb_host_device_snapshot(devices, USB_HOST_DEVICE_MAX);
	for (size_t i = 0; i < count; i++)
		if (!append_text(node, devices[i].type) || !append_text(node, " ") ||
		    !append_u64(node, devices[i].index) || !append_text(node, "\n")) goto overflow;
	node->vnode.size = node->content_length; *result = node; return 0;
overflow:
	vnode_put(&node->vnode); return -EINVAL;
}

static int
procfs_make_disks(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("disks", PROCFS_DISKS, VNODE_REGULAR, inode);
	if (node == NULL) return -ENOMEM;
	if (!append_text(node, "NAME SECTORS SECTOR_SIZE BYTES\n")) {
		vnode_put(&node->vnode); return -EINVAL;
	}
	for (size_t i = 0; i < block_device_count(); i++) {
		struct block_device *device = block_device_at(i);
		if (device == NULL || !append_text(node, device->name) || !append_text(node, " ") ||
		    !append_u64(node, device->sector_count) || !append_text(node, " ") ||
		    !append_u64(node, device->logical_sector_size) || !append_text(node, " ") ||
		    !append_u64(node, device->sector_count * device->logical_sector_size) || !append_text(node, "\n")) {
			vnode_put(&node->vnode); return -EINVAL;
		}
	}
	node->vnode.size = node->content_length; *result = node; return 0;
}

static struct procfs_node *
procfs_allocate(const char *name, enum procfs_entry entry,
	enum vnode_type type, uint64_t inode)
{
	struct procfs_node *node;

	if (inode == 0)
		inode = vfs_allocate_inode();
	if (inode == 0)
		return NULL;
	node = kheap_alloc(sizeof(*node));
	if (node == NULL)
		return NULL;
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, strlen(name) + 1U);
	node->entry = entry;
	node->vnode = (struct vnode){
		.operations = &procfs_operations,
		.type = type,
		.mode = type == VNODE_DIRECTORY ? 0555U : 0444U,
		.inode = inode,
		.references = 1,
		.private_data = node
	};
	return node;
}

static int
procfs_make_self(uint64_t inode, struct procfs_node **result)
{
	struct process_snapshot_entry current;
	struct procfs_node *node;
	static const char *const states[] = {
		[PROCESS_READY] = "ready",
		[PROCESS_RUNNING] = "running",
		[PROCESS_BLOCKED] = "blocked",
		[PROCESS_ZOMBIE] = "zombie"
	};
	int error;

	error = process_current_snapshot(&current);
	if (error != 0)
		return error;
	node = procfs_allocate("self", PROCFS_SELF, VNODE_REGULAR, inode);
	if (node == NULL)
		return -ENOMEM;
	if (!append_text(node, "Pid:\t") || !append_u64(node, current.pid) ||
	    !append_text(node, "\nPPid:\t") ||
	    !append_u64(node, current.parent_pid) ||
	    !append_text(node, "\nState:\t") ||
	    current.state >= sizeof(states) / sizeof(states[0]) ||
	    !append_text(node, states[current.state]) ||
	    !append_text(node, "\nExitStatus:\t") ||
	    !append_u64(node, current.exit_status) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -EINVAL;
	}
	node->content[node->content_length] = '\0';
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_make_uptime(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("uptime", PROCFS_UPTIME,
	    VNODE_REGULAR, inode);
	uint64_t ticks;
	uint64_t fractional;

	if (node == NULL)
		return -ENOMEM;
	ticks = timer_ticks();
	fractional = ticks % PROCFS_TICK_HZ;
	if (!append_u64(node, ticks / PROCFS_TICK_HZ) ||
	    !append_text(node, ".") ||
	    !append_text(node, (const char[]){
		(char)('0' + fractional / 10U),
		(char)('0' + fractional % 10U), '\0'
	    }) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -EINVAL;
	}
	node->content[node->content_length] = '\0';
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_make_meminfo(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("meminfo", PROCFS_MEMINFO,
	    VNODE_REGULAR, inode);
	uint64_t total = pmm_usable_pages();
	uint64_t free = pmm_free_pages();

	if (node == NULL)
		return -ENOMEM;
	/* PMM frames are 4096 bytes; report KiB in one immutable snapshot. */
	if (!append_text(node, "MemTotal: ") || !append_u64(node, total * 4U) ||
	    !append_text(node, " kB\nMemFree: ") ||
	    !append_u64(node, free * 4U) || !append_text(node, " kB\n")) {
		vnode_put(&node->vnode);
		return -EINVAL;
	}
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static void
procfs_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
	uint32_t *edx)
{
	__asm__ volatile ("cpuid" : "=a" (*eax), "=b" (*ebx), "=c" (*ecx),
	    "=d" (*edx) : "a" (leaf), "c" (0) : "memory");
}

static void
procfs_store_word(char *buffer, size_t offset, uint32_t word)
{
	for (size_t i = 0; i < 4; i++)
		buffer[offset + i] = (char)(word >> (i * 8U));
}

static int
procfs_make_cpuinfo(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("cpuinfo", PROCFS_CPUINFO,
	    VNODE_REGULAR, inode);
	uint32_t max_leaf, eax, ebx, ecx, edx;
	uint32_t signature = 0;
	uint32_t base_family, family, base_model, model, stepping;
	char vendor[13] = "UnknownCPU";
	char brand[49] = "Unknown processor";
	size_t brand_start = 0;

	if (node == NULL)
		return -ENOMEM;
	procfs_cpuid(0, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf >= 1U) {
		procfs_cpuid(1, &signature, &ebx, &ecx, &edx);
		procfs_store_word(vendor, 0, ebx);
		procfs_store_word(vendor, 4, edx);
		procfs_store_word(vendor, 8, ecx);
		vendor[12] = '\0';
	}
	procfs_cpuid(0x80000000U, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf >= 0x80000004U) {
		for (uint32_t i = 0; i < 3U; i++) {
			uint32_t leaf = 0x80000002U + i;
			procfs_cpuid(leaf, &eax, &ebx, &ecx, &edx);
			procfs_store_word(brand, i * 16U, eax);
			procfs_store_word(brand, i * 16U + 4U, ebx);
			procfs_store_word(brand, i * 16U + 8U, ecx);
			procfs_store_word(brand, i * 16U + 12U, edx);
		}
		brand[48] = '\0';
	}
	for (size_t i = strlen(brand); i != 0 && brand[i - 1U] == ' '; i--)
		brand[i - 1U] = '\0';
	while (brand[brand_start] == ' ')
		brand_start++;
	base_family = (signature >> 8) & 0x0fU;
	base_model = (signature >> 4) & 0x0fU;
	family = base_family == 0x0fU ? base_family +
	    ((signature >> 20) & 0xffU) : base_family;
	model = (base_family == 0x06U || base_family == 0x0fU) ?
	    base_model | (((signature >> 16) & 0x0fU) << 4) : base_model;
	stepping = signature & 0x0fU;
	if (!append_text(node, "processors discovered: ") ||
	    !append_u64(node, smp_discovered_count()) ||
	    !append_text(node, "\nprocessors online: ") ||
	    !append_u64(node, smp_online_count()) ||
	    !append_text(node, "\nprocessor: 0\nvendor_id: ") ||
	    !append_text(node, vendor) || !append_text(node, "\ncpu family: ") ||
	    !append_u64(node, family) || !append_text(node, "\nmodel: ") ||
	    !append_u64(node, model) || !append_text(node, "\nstepping: ") ||
	    !append_u64(node, stepping) || !append_text(node, "\nmodel name: ") ||
	    !append_text(node, brand + brand_start) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -EINVAL;
	}
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_make_modules(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("modules", PROCFS_MODULES,
	    VNODE_REGULAR, inode);
	struct kernel_module_snapshot_entry entries[KERNEL_MODULE_LIMIT];
	size_t count;
	if (node == NULL)
		return -ENOMEM;
	count = kernel_module_snapshot(entries,
	    sizeof(entries) / sizeof(entries[0]));
	for (size_t i = 0; i < count; i++) {
		if (!append_text(node, entries[i].name) || !append_text(node, "\t") ||
		    !append_u64(node, entries[i].references) ||
		    !append_text(node, "\n")) {
			vnode_put(&node->vnode);
			return -EINVAL;
		}
	}
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_lookup(struct vnode *directory, const char *name, size_t name_length,
	struct vnode **result)
{
	struct procfs_node *node;
	int error;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -ENOTDIR;
	if (name == NULL || result == NULL)
		return -EINVAL;
	*result = NULL;
	node = procfs_from_vnode(directory);
	if (node->entry != PROCFS_ROOT)
		return -ENOTDIR;
	if (name_length == 4 && memcmp(name, "self", 4) == 0) {
		struct procfs_node *self;

		error = procfs_make_self(node->self_inode, &self);
		if (error == 0)
			*result = &self->vnode;
		return error;
	}
	if (name_length == 6 && memcmp(name, "uptime", 6) == 0) {
		struct procfs_node *uptime;

		error = procfs_make_uptime(node->uptime_inode, &uptime);
		if (error == 0)
			*result = &uptime->vnode;
		return error;
	}
	if (name_length == 7 && memcmp(name, "meminfo", 7) == 0) {
		struct procfs_node *meminfo;

		error = procfs_make_meminfo(node->meminfo_inode, &meminfo);
		if (error == 0)
			*result = &meminfo->vnode;
		return error;
	}
	if (name_length == 7 && memcmp(name, "cpuinfo", 7) == 0) {
		struct procfs_node *cpuinfo;

		error = procfs_make_cpuinfo(node->cpuinfo_inode, &cpuinfo);
		if (error == 0)
			*result = &cpuinfo->vnode;
		return error;
	}
	if (name_length == 7 && memcmp(name, "modules", 7) == 0) {
		struct procfs_node *modules;

		error = procfs_make_modules(node->modules_inode, &modules);
		if (error == 0)
			*result = &modules->vnode;
		return error;
	}
    bool tone_test = name_length == 7 && memcmp(name, "hdatest", 7) == 0;
    if (tone_test || (name_length == 9 && memcmp(name, "hdacodecs", 9) == 0)) {
        struct os_credentials credentials;
        if (!process_current_credentials(&credentials) || credentials.effective_uid != 0)
            return -1;
        struct procfs_node *entry = procfs_allocate(tone_test ? "hdatest" : "hdacodecs", PROCFS_HDACODECS,
            VNODE_REGULAR, tone_test ? node->hdatest_inode : node->hdacodecs_inode);
        if (!entry) return -ENOMEM;
        entry->vnode.mode = 0400;
        error = (tone_test ? hda_tone_snapshot : hda_probe_snapshot)(entry->content,
            sizeof(entry->content), &entry->content_length);
        if (error) { vnode_put(&entry->vnode); return error; }
        entry->vnode.size = entry->content_length;
        *result = &entry->vnode;
        return 0;
    }
    if (name_length == 8 && memcmp(name, "audiopci", 8) == 0) {
        struct procfs_node *entry;
        error = procfs_make_audiopci(node->audiopci_inode, &entry);
        if (error == 0) *result = &entry->vnode;
        return error;
    }
	bool network_pci = name_length == 6 && memcmp(name, "netpci", 6) == 0;
	if (network_pci || (name_length == 3 && memcmp(name, "pci", 3) == 0)) {
		struct procfs_node *entry;
		error = procfs_make_pci(network_pci ? node->netpci_inode :
		    node->pci_inode, &entry, network_pci);
		if (error == 0) *result = &entry->vnode;
		return error;
	}
	if (name_length == 3 && memcmp(name, "usb", 3) == 0) {
		struct procfs_node *entry;
		error = procfs_make_usb(node->usb_inode, &entry);
		if (error == 0) *result = &entry->vnode;
		return error;
	}
	if (name_length == 5 && memcmp(name, "disks", 5) == 0) {
		struct procfs_node *entry;
		error = procfs_make_disks(node->disks_inode, &entry);
		if (error == 0) *result = &entry->vnode;
		return error;
	}
	if (name_length == 5 && memcmp(name, "netif", 5) == 0) {
		struct procfs_node *entry = procfs_allocate("netif", PROCFS_NETIF,
		    VNODE_REGULAR, node->netif_inode);
		if (entry == NULL)
			return -ENOMEM;
		const char *device = cyclone_net_device_name();
		if (device != NULL && !append_text(entry, device)) {
			vnode_put(&entry->vnode);
			return -EINVAL;
		}
		entry->vnode.size = entry->content_length;
		*result = &entry->vnode;
		return 0;
	}
	if (name_length == 7 && memcmp(name, "cmdline", 7) == 0) {
		struct procfs_node *entry = procfs_allocate("cmdline",
		    PROCFS_CMDLINE, VNODE_REGULAR, node->cmdline_inode);
		if (!entry)
			return -ENOMEM;
		if (!append_text(entry, boot_cmdline) ||
		    !append_text(entry, "\n")) {
			vnode_put(&entry->vnode);
			return -EINVAL;
		}
		entry->vnode.size = entry->content_length;
		*result = &entry->vnode;
		return 0;
	}
	if (name_length == 4 && memcmp(name, "dapi", 4) == 0) {
		struct procfs_node *entry = procfs_allocate("dapi",
		    PROCFS_DAPI, VNODE_REGULAR, node->dapi_inode);
		if (entry == NULL)
			return -ENOMEM;
		uint32_t version = kernel_driver_api_version();
		if (!append_u64(entry, version >> 16) || !append_text(entry, ".") ||
		    !append_u64(entry, version & 0xffffU) || !append_text(entry, "\n")) {
			vnode_put(&entry->vnode);
			return -EINVAL;
		}
		entry->vnode.size = entry->content_length;
		*result = &entry->vnode;
		return 0;
	}
	return -ENOENT;
}

static int
procfs_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct procfs_node *node;
	size_t available;

	if (vnode == NULL || read_length == NULL ||
	    (buffer == NULL && length != 0))
		return -EINVAL;
	*read_length = 0;
	if (vnode->type == VNODE_DIRECTORY)
		return -EINVAL;
	node = procfs_from_vnode(vnode);
	if (offset >= node->content_length)
		return 0;
	available = node->content_length - (size_t)offset;
	if (length > available)
		length = available;
	if (length != 0)
		memcpy(buffer, node->content + (size_t)offset, length);
	*read_length = length;
	return 0;
}

static int
procfs_readdir(struct vnode *directory, uint64_t index,
	struct vfs_dirent *entry)
{
	static const char *const names[] = {"self", "uptime", "meminfo",
	    "cpuinfo", "modules", "pci", "usb", "disks", "cmdline", "netpci",
	    "netif", "dapi", "audiopci", "hdacodecs", "hdatest"};
	struct procfs_node *node;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -ENOTDIR;
	if (entry == NULL)
		return -EINVAL;
	if (index >= sizeof(names) / sizeof(names[0]))
		return 0;
	node = procfs_from_vnode(directory);
	memset(entry, 0, sizeof(*entry));
	entry->inode = index == 0 ? node->self_inode
	    : index == 1	  ? node->uptime_inode
	    : index == 2	  ? node->meminfo_inode
	    : index == 3	  ? node->cpuinfo_inode
	    : index == 4	  ? node->modules_inode
	    : index == 5	  ? node->pci_inode
	    : index == 6	  ? node->usb_inode
	    : index == 7	  ? node->disks_inode
				  : index == 8	  ? node->cmdline_inode
				  : index == 9       ? node->netpci_inode
				  : index == 10      ? node->netif_inode
							  : index == 11 ? node->dapi_inode : index == 12 ? node->audiopci_inode : index == 13 ? node->hdacodecs_inode : node->hdatest_inode;
	entry->type = VNODE_REGULAR;
	memcpy(entry->name, names[index], strlen(names[index]) + 1U);
	return 1;
}

static void
procfs_destroy(struct vnode *vnode)
{
	(void)kheap_free(procfs_from_vnode(vnode));
}

int
procfs_mount(const char *path)
{
	struct procfs_node *root = procfs_allocate("", PROCFS_ROOT,
	    VNODE_DIRECTORY, 0);
	int error;

	if (root == NULL)
		return -ENOMEM;
	root->self_inode = vfs_allocate_inode();
	root->uptime_inode = vfs_allocate_inode();
	root->meminfo_inode = vfs_allocate_inode();
	root->cpuinfo_inode = vfs_allocate_inode();
	root->modules_inode = vfs_allocate_inode();
	root->pci_inode = vfs_allocate_inode();
	root->usb_inode = vfs_allocate_inode();
	root->disks_inode = vfs_allocate_inode();
	root->cmdline_inode = vfs_allocate_inode();
	root->netpci_inode = vfs_allocate_inode();
	root->netif_inode = vfs_allocate_inode();
	root->dapi_inode = vfs_allocate_inode();
	root->audiopci_inode = vfs_allocate_inode();
	root->hdacodecs_inode = vfs_allocate_inode();
	root->hdatest_inode = vfs_allocate_inode();
	if (root->self_inode == 0 || root->uptime_inode == 0 ||
	    root->meminfo_inode == 0 || root->cpuinfo_inode == 0 ||
	    root->modules_inode == 0 || root->pci_inode == 0 ||
	    root->usb_inode == 0 || root->disks_inode == 0 ||
	    root->cmdline_inode == 0 || root->netpci_inode == 0 ||
	    root->netif_inode == 0 || root->dapi_inode == 0 || root->audiopci_inode == 0 || root->hdacodecs_inode == 0 || root->hdatest_inode == 0) {
		vnode_put(&root->vnode);
		return -ENOMEM;
	}
	error = vfs_mount(path, &root->vnode);
	vnode_put(&root->vnode);
	return error;
}
