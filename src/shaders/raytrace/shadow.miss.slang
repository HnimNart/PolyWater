#include "shared.h.slang"

[shader("miss")] void main(inout ShadowPayload payload)
{
  payload.isHit =
      false;  // If we reach the miss shader, nothing blocked the light
}
