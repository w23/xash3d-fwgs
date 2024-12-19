#include "vk_core.h"
#include "vk_image.h"

// TODO this needs to be negotiated by swapchain creation
// however, currently render pass also needs it so ugh
#define SWAPCHAIN_FORMAT VK_FORMAT_B8G8R8A8_UNORM //SRGB
//#define SWAPCHAIN_FORMAT VK_FORMAT_B8G8R8A8_SRGB

// TODO: move render pass and depth format away from this
qboolean R_VkSwapchainInit( VkRenderPass pass, VkFormat depth_format );
void R_VkSwapchainShutdown( void );

typedef struct {
	uint32_t index;
	// Non-owned image mostly for for sync/barrier tracking purposes
	r_vk_image_t image;
	VkFramebuffer framebuffer;
} r_vk_swapchain_framebuffer_t;

r_vk_swapchain_framebuffer_t R_VkSwapchainAcquire( VkSemaphore sem_image_available );

void R_VkSwapchainPresent( uint32_t index, VkSemaphore done );
