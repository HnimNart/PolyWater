#include "entrypoints/raytrace/ray_common.h.slang"

/**********************************************************/
[shader("miss")] void main(inout ShadowPayload payload)
/**********************************************************/
{
  payload.isHit = false;
}
