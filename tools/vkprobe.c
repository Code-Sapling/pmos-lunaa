/* Minimal Vulkan probe for realme-lunaa.
 *
 * vulkaninfo enables every instance extension it can find, including
 * VK_KHR_display -- and turnip's KGSL backend refuses to load in that case
 * (tu_knl_kgsl.cc:1783, "I can't KHR_display"). So it never reaches the
 * device. This creates a bare instance with ZERO extensions, which is the
 * only way to actually exercise the KGSL path.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

int main(void)
{
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkprobe",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
		.enabledExtensionCount = 0,
		.enabledLayerCount = 0,
	};

	VkInstance inst;
	VkResult r = vkCreateInstance(&ci, NULL, &inst);
	if (r != VK_SUCCESS) {
		printf("vkCreateInstance failed: %d\n", r);
		return 1;
	}

	uint32_t n = 0;
	r = vkEnumeratePhysicalDevices(inst, &n, NULL);
	if (r != VK_SUCCESS) {
		printf("vkEnumeratePhysicalDevices failed: %d\n", r);
		return 1;
	}
	printf("physical devices: %u\n", n);
	if (!n)
		return 1;

	VkPhysicalDevice pd[8];
	if (n > 8)
		n = 8;
	vkEnumeratePhysicalDevices(inst, &n, pd);

	for (uint32_t i = 0; i < n; i++) {
		VkPhysicalDeviceDrmPropertiesEXT drm = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
		};
		VkPhysicalDeviceDriverProperties drv = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
			.pNext = &drm,
		};
		VkPhysicalDeviceProperties2 p2 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &drv,
		};
		vkGetPhysicalDeviceProperties2(pd[i], &p2);

		VkPhysicalDeviceMemoryProperties mem;
		vkGetPhysicalDeviceMemoryProperties(pd[i], &mem);
		VkDeviceSize heap = 0;
		for (uint32_t h = 0; h < mem.memoryHeapCount; h++)
			if (mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				heap += mem.memoryHeaps[h].size;

		printf("\n[%u] %s\n", i, p2.properties.deviceName);
		printf("    driver      : %s (%s)\n", drv.driverName, drv.driverInfo);
		printf("    api version : %u.%u.%u\n",
		       VK_VERSION_MAJOR(p2.properties.apiVersion),
		       VK_VERSION_MINOR(p2.properties.apiVersion),
		       VK_VERSION_PATCH(p2.properties.apiVersion));
		printf("    device type : %u\n", p2.properties.deviceType);
		printf("    local heap  : %llu MB\n",
		       (unsigned long long)(heap / (1024 * 1024)));

		/* Can we export dmabufs? That is the whole ballgame for ever
		 * getting this in front of a compositor. */
		uint32_t ec = 0;
		vkEnumerateDeviceExtensionProperties(pd[i], NULL, &ec, NULL);
		VkExtensionProperties *ex = calloc(ec, sizeof(*ex));
		vkEnumerateDeviceExtensionProperties(pd[i], NULL, &ec, ex);
		int fd = 0, mod = 0, pdrm = 0;
		for (uint32_t e = 0; e < ec; e++) {
			if (!strcmp(ex[e].extensionName, "VK_KHR_external_memory_fd"))
				fd = 1;
			if (!strcmp(ex[e].extensionName, "VK_EXT_image_drm_format_modifier"))
				mod = 1;
			if (!strcmp(ex[e].extensionName, "VK_EXT_physical_device_drm"))
				pdrm = 1;
		}
		free(ex);
		printf("    device extensions: %u\n", ec);
		printf("    VK_KHR_external_memory_fd        : %s\n", fd ? "yes" : "NO");
		printf("    VK_EXT_image_drm_format_modifier : %s\n", mod ? "yes" : "NO");
		printf("    VK_EXT_physical_device_drm       : %s\n", pdrm ? "yes" : "NO");

		/* This is what wlroots and zink match on to select a GPU. On
		 * stock turnip+KGSL the extension is absent and they refuse
		 * the device; with the local patch it should name the display
		 * DRM node (226:128 for renderD128). */
		printf("    drmHasRender  : %s  render %lld:%lld\n",
		       drm.hasRender ? "yes" : "NO",
		       (long long)drm.renderMajor, (long long)drm.renderMinor);
		printf("    drmHasPrimary : %s  primary %lld:%lld\n",
		       drm.hasPrimary ? "yes" : "NO",
		       (long long)drm.primaryMajor, (long long)drm.primaryMinor);
	}

	return 0;
}
