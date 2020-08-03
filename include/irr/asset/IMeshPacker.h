#ifndef __IRR_I_MESH_PACKER_H_INCLUDED__
#define __IRR_I_MESH_PACKER_H_INCLUDED__

namespace irr
{
namespace asset
{

class MeshPackerBase
{
public:
    struct AllocationParams
    {
        size_t indexBuffSupportedCnt                   = 1073741824ull;      /*   2GB*/
        size_t vertexBuffSupportedCnt                  = 107374182ull;       /*   2GB assuming vertex size is 20B*/
        size_t perInstanceVertexBuffSupportedCnt       = 3355443ull;         /*  32MB assuming per instance vertex attrib size is 10B*/
        size_t MDIDataBuffSupportedCnt                 = 16777216ull;        /*  16MB assuming MDIStructType is DrawElementsIndirectCommand_t*/
        size_t vertexBufferMinAllocSize                = 32ull;
        size_t indexBufferMinAllocSize                 = 256ull;
        size_t perInstanceVertexBufferMinAllocSize     = 32ull;
        size_t MDIDataBuffMinAllocSize                 = 32ull;
    };

    template <typename BufferType>
    struct PackedMeshBuffer
    {
        //or output should look more like `return_type` from geometry creator?
        //TODO: add parameters of the 
        core::smart_refctd_ptr<BufferType> MDIDataBuffer;
        SBufferBinding<BufferType> vertexBufferBindings[SVertexInputParams::MAX_ATTR_BUF_BINDING_COUNT] = {};
        SBufferBinding<BufferType> indexBuffer;

        SVertexInputParams vertexInputParams;

        inline bool isValid()
        {
            return this->MDIDataBuffer->getPointer() != nullptr;
        }
    };

    struct ReservedAllocationMeshBuffers
    {
        uint32_t mdiAllocationOffset;
        uint32_t mdiAllocationReservedSize;
        uint32_t instanceAllocationOffset;
        uint32_t instanceAllocationReservedSize;
        uint32_t indexAllocationOffset;
        uint32_t indexAllocationReservedSize;
        uint32_t vertexAllocationOffset;
        uint32_t vertexAllocationReservedSize;

        inline bool isValid()
        {
            return this->mdiAllocationOffset != core::GeneralpurposeAddressAllocator<uint32_t>::invalid_address;
        }
    };

    struct PackedMeshBufferData
    {
        uint32_t mdiParameterOffset; // add to `CCPUMeshPacker::getMultiDrawIndirectBuffer()->getPointer() to get `DrawElementsIndirectCommand_t` address
        uint32_t mdiParameterCount;

        inline bool isValid()
        {
            return this->mdiParameterOffset != core::GeneralpurposeAddressAllocator<uint32_t>::invalid_address;
        }
    };

protected:
    MeshPackerBase(const AllocationParams& allocParams)
        :m_allocParams(allocParams) {};

protected:
    const AllocationParams m_allocParams;

};

//TODO: allow mesh buffers with only per instance attributes

template <typename MeshBufferType, typename MDIStructType>
class IMeshPacker : public MeshPackerBase
{
    static_assert(std::is_base_of<DrawElementsIndirectCommand_t, MDIStructType>::value);

public:
    IMeshPacker(const SVertexInputParams& preDefinedLayout, const AllocationParams& allocParams, uint16_t minTriangleCountPerMDIData, uint16_t maxTriangleCountPerMDIData)
        :MeshPackerBase(allocParams),
         m_maxTriangleCountPerMDIData(maxTriangleCountPerMDIData),
         m_minTriangleCountPerMDIData(minTriangleCountPerMDIData),
         m_MDIDataAlctrResSpc(nullptr), m_idxBuffAlctrResSpc(nullptr),
         m_vtxBuffAlctrResSpc(nullptr), m_perInsVtxBuffAlctrResSpc(nullptr)
    {
        m_outVtxInputParams.enabledAttribFlags  = preDefinedLayout.enabledAttribFlags;
        m_outVtxInputParams.enabledBindingFlags = preDefinedLayout.enabledAttribFlags;

        memcpy(m_outVtxInputParams.attributes, preDefinedLayout.attributes, sizeof(m_outVtxInputParams.attributes));

        m_vtxSize = calcVertexSize(preDefinedLayout, E_VERTEX_INPUT_RATE::EVIR_PER_VERTEX);
        assert(m_vtxSize);
        if (m_vtxSize)
        {
            m_vtxBuffAlctrResSpc = _IRR_ALIGNED_MALLOC(core::GeneralpurposeAddressAllocator<uint32_t>::reserved_size(alignof(std::max_align_t), allocParams.vertexBuffSupportedCnt, allocParams.vertexBufferMinAllocSize), _IRR_SIMD_ALIGNMENT);
            //for now mesh packer will not allow mesh buffers without any per vertex attributes
            _IRR_DEBUG_BREAK_IF(m_vtxBuffAlctrResSpc == nullptr);
            assert(m_vtxBuffAlctrResSpc != nullptr);
            m_vtxBuffAlctr = core::GeneralpurposeAddressAllocator<uint32_t>(m_vtxBuffAlctrResSpc, 0u, 0u, alignof(std::max_align_t), allocParams.vertexBuffSupportedCnt, allocParams.vertexBufferMinAllocSize);
        }
        
        m_perInstVtxSize = calcVertexSize(preDefinedLayout, E_VERTEX_INPUT_RATE::EVIR_PER_INSTANCE);
        if (m_perInstVtxSize)
        {
            m_perInsVtxBuffAlctrResSpc = _IRR_ALIGNED_MALLOC(core::GeneralpurposeAddressAllocator<uint32_t>::reserved_size(alignof(std::max_align_t), allocParams.perInstanceVertexBuffSupportedCnt, allocParams.perInstanceVertexBufferMinAllocSize), _IRR_SIMD_ALIGNMENT);
            _IRR_DEBUG_BREAK_IF(m_perInsVtxBuffAlctrResSpc == nullptr);
            assert(m_perInsVtxBuffAlctrResSpc != nullptr);
            m_perInsVtxBuffAlctr = core::GeneralpurposeAddressAllocator<uint32_t>(m_perInsVtxBuffAlctrResSpc, 0u, 0u, alignof(std::max_align_t), allocParams.perInstanceVertexBuffSupportedCnt, allocParams.perInstanceVertexBufferMinAllocSize);
        }

        m_idxBuffAlctrResSpc = _IRR_ALIGNED_MALLOC(core::GeneralpurposeAddressAllocator<uint32_t>::reserved_size(alignof(uint16_t), allocParams.indexBuffSupportedCnt, allocParams.indexBufferMinAllocSize), _IRR_SIMD_ALIGNMENT);
        _IRR_DEBUG_BREAK_IF(m_idxBuffAlctrResSpc == nullptr);
        assert(m_idxBuffAlctrResSpc != nullptr);
        m_idxBuffAlctr = core::GeneralpurposeAddressAllocator<uint32_t>(m_idxBuffAlctrResSpc, 0u, 0u, alignof(uint16_t), allocParams.indexBuffSupportedCnt, allocParams.indexBufferMinAllocSize);

        m_MDIDataAlctrResSpc = _IRR_ALIGNED_MALLOC(core::GeneralpurposeAddressAllocator<uint32_t>::reserved_size(alignof(MDIStructType), allocParams.MDIDataBuffSupportedCnt, allocParams.MDIDataBuffMinAllocSize), _IRR_SIMD_ALIGNMENT);
        _IRR_DEBUG_BREAK_IF(m_MDIDataAlctrResSpc == nullptr);
        assert(m_MDIDataAlctrResSpc != nullptr);
        m_MDIDataAlctr = core::GeneralpurposeAddressAllocator<uint32_t>(m_MDIDataAlctrResSpc, 0u, 0u, alignof(MDIStructType), allocParams.MDIDataBuffSupportedCnt, allocParams.MDIDataBuffMinAllocSize);

        //1 attrib enabled == 1 binding
        for (uint16_t attrBit = 0x0001, location = 0; location < SVertexInputParams::MAX_ATTR_BUF_BINDING_COUNT; attrBit <<= 1, location++)
        {
            if (m_outVtxInputParams.enabledAttribFlags & attrBit)
            {
                m_outVtxInputParams.attributes[location].binding = location;
                m_outVtxInputParams.attributes[location].relativeOffset = 0u;
                m_outVtxInputParams.bindings[location].stride = getTexelOrBlockBytesize(static_cast<E_FORMAT>(m_outVtxInputParams.attributes[location].format));
                m_outVtxInputParams.bindings[location].inputRate = preDefinedLayout.bindings[preDefinedLayout.attributes[location].binding].inputRate;
            }
        }
    }

protected:
    virtual ~IMeshPacker() 
    {
        _IRR_ALIGNED_FREE(m_MDIDataAlctrResSpc);
        _IRR_ALIGNED_FREE(m_idxBuffAlctrResSpc);
        _IRR_ALIGNED_FREE(m_vtxBuffAlctrResSpc);
        _IRR_ALIGNED_FREE(m_perInsVtxBuffAlctrResSpc);
    }

    inline size_t calcVertexSize(const SVertexInputParams& vtxInputParams, const E_VERTEX_INPUT_RATE inputRate) const
    {
        size_t size = 0ull;
        for (size_t i = 0; i < SVertexInputParams::MAX_VERTEX_ATTRIB_COUNT; ++i)
        {
            if (vtxInputParams.enabledAttribFlags & (1u << i))
                if(vtxInputParams.bindings[i].inputRate == inputRate)
                    size += asset::getTexelOrBlockBytesize(static_cast<E_FORMAT>(vtxInputParams.attributes[i].format));
        }

        return size;
    }

public:
    struct Triangle
    {
        uint32_t oldIndices[3];
    };

    struct TriangleBatch
    {
        core::vector<Triangle> triangles;
    };

    virtual core::vector<TriangleBatch> constructTriangleBatches(MeshBufferType* meshBuffer) = 0;

protected:
    //output mesh buffers data
    SVertexInputParams m_outVtxInputParams;

    void* m_MDIDataAlctrResSpc;
    void* m_idxBuffAlctrResSpc;
    void* m_vtxBuffAlctrResSpc;
    void* m_perInsVtxBuffAlctrResSpc;
    core::GeneralpurposeAddressAllocator<uint32_t> m_vtxBuffAlctr;
    core::GeneralpurposeAddressAllocator<uint32_t> m_idxBuffAlctr;
    core::GeneralpurposeAddressAllocator<uint32_t> m_perInsVtxBuffAlctr;
    core::GeneralpurposeAddressAllocator<uint32_t> m_MDIDataAlctr;

    const uint16_t m_minTriangleCountPerMDIData;
    const uint16_t m_maxTriangleCountPerMDIData;

    uint32_t m_vtxSize;
    uint32_t m_perInstVtxSize;

    _IRR_STATIC_INLINE_CONSTEXPR uint32_t INVALID_ADDRESS = core::GeneralpurposeAddressAllocator<uint32_t>::invalid_address;
    _IRR_STATIC_INLINE_CONSTEXPR ReservedAllocationMeshBuffers invalidReservedAllocationMeshBuffers{ INVALID_ADDRESS, 0, 0, 0, 0, 0, 0, 0 };
};

}
}

#endif