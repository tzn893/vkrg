#include "vkrg/prototypes.h"

bool vkrg::RenderGraphImageExtent::operator==(const RenderGraphImageExtent& ext)
{
    if (ext.fit_to_screen != fit_to_screen)
    {
        return false;
    }
    else
    {
        if (ext.fit_to_screen)
        {
            return ext.ext.screen_scale == this->ext.screen_scale;
        }
        
        return ext.ext.height == this->ext.height && ext.ext.width == this->ext.width && ext.ext.depth == this->ext.depth;
    }
}
