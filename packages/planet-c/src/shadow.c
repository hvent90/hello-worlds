#include "shadow.h"

#include "rlgl.h"

// Load render texture for shadowmap projection
RenderTexture2D LoadShadowmapRenderTexture(int width, int height) {
  RenderTexture2D target = {0};

  target.id = rlLoadFramebuffer(); // Load an empty framebuffer
  target.texture.width = width;
  target.texture.height = height;

  if (target.id > 0) {
    rlEnableFramebuffer(target.id);

    // Create depth texture
    target.depth.id = rlLoadTextureDepth(width, height, false);
    target.depth.width = width;
    target.depth.height = height;
    target.depth.format = 19; // DEPTH_COMPONENT_24BIT?
    target.depth.mipmaps = 1;

    // Attach depth texture to FBO
    rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH,
                        RL_ATTACHMENT_TEXTURE2D, 0);

    // Check if fbo is complete with attachments (valid)
    if (rlFramebufferComplete(target.id))
      TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully",
               target.id);

    rlDisableFramebuffer();
  } else
    TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");

  return target;
}