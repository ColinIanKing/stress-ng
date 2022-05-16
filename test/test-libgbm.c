#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>

int main(void)
{
    struct gbm_device *gbm;
    struct gbm_surface *gs;
    static const char *devicenode = "/dev/dri/renderD128";
    const uint32_t size_x = 16, size_y = 16;
    int fd = open(devicenode, O_RDWR);
    if(fd < 0)
        return 1;

    gbm = gbm_create_device(fd);
        if(gbm == NULL)
            return 1;
    gs = gbm_surface_create(gbm, size_x, size_y, GBM_BO_FORMAT_ARGB8888,
				GBM_BO_USE_LINEAR|GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING);
	if(!gs){
		(void)fprintf(stderr, "Could not create gl drm window \n");
        return 1;
    }
    (void)close(fd);
    return 0;

}