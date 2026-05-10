// Reduction Operations
#define REDUCE_OP_MAX 0
#define REDUCE_OP_MIN 1
#define REDUCE_OP_AVG 2

struct PushData {
    int isFirstPass;
    int reductionOp;
};

[[vk::push_constant]]
ConstantBuffer<PushData> pushConst;

[[vk::binding(0, 0)]] Texture2D<float> inDepthMip;
[[vk::binding(1, 0)]] RWTexture2D<float> outDepthMip;

/**********************************************************/
float reduce(float d0, float d1, float d2, float d3, int op)
/**********************************************************/
{
    switch(op)
    {
        case REDUCE_OP_MIN:
            return min(min(d0, d1), min(d2, d3));
        case REDUCE_OP_AVG:
            return (d0 + d1 + d2 + d3) * 0.25f;
        case REDUCE_OP_MAX:
        default:
            return max(max(d0, d1), max(d2, d3));
    }
}

/**********************************************************/
[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID) 
/**********************************************************/
{
    uint2 outDim;
    outDepthMip.GetDimensions(outDim.x, outDim.y);

    if (threadId.x >= outDim.x || threadId.y >= outDim.y) return;

    if (pushConst.isFirstPass == 1) 
    {
        float depth = inDepthMip.Load(int3(threadId.xy, 0)).r;
        outDepthMip[threadId.xy] = depth;
    } 
    else 
    {
        int2 inPos = int2(threadId.xy * 2);
        float d0 = inDepthMip.Load(int3(inPos, 0)).r;
        float d1 = inDepthMip.Load(int3(inPos + int2(1, 0), 0)).r;
        float d2 = inDepthMip.Load(int3(inPos + int2(0, 1), 0)).r;
        float d3 = inDepthMip.Load(int3(inPos + int2(1, 1), 0)).r;

        outDepthMip[threadId.xy] = reduce(d0, d1, d2, d3, pushConst.reductionOp);
    }
}

