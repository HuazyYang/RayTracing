#ifndef CL_CONTEXT_HPP
#define CL_CONTEXT_HPP

#include "scene/scene.hpp"
#include <CL/cl.hpp>
#include <memory>
#include <Windows.h>

// OpenCL & OpenGL interop
// https://software.intel.com/en-us/articles/opencl-and-opengl-interoperability-tutorial

enum RenderKernelArgument_t : unsigned int
{
    BUFFER_OUT,
    BUFFER_SCENE,
    BUFFER_NODE,
    BUFFER_MATERIAL,
    WIDTH,
    HEIGHT,
    CAM_ORIGIN,
    CAM_FRONT,
    CAM_UP,
    FRAME_COUNT,
    FRAME_SEED,
    TEXTURE0,
};

class CLKernel;

class CLContext
{
public:
    CLContext(const cl::Platform& platform, HDC display_context, HGLRC gl_context);

    void WriteBuffer(const cl::Buffer& buffer, const void* data, size_t size) const;
    void ReadBuffer(const cl::Buffer& buffer, void* ptr, size_t size) const;
    void ExecuteKernel(std::shared_ptr<CLKernel> kernel, size_t workSize) const;
    void Finish() const { m_Queue.finish(); }
    void AcquireGLObject(cl_mem mem);
    void ReleaseGLObject(cl_mem mem);

    const cl::Context& GetContext() const { return m_Context; }

private:
    cl::Context m_Context;
    cl::CommandQueue m_Queue;

};

class CLKernel
{
public:
    CLKernel(const char* filename, const CLContext& cl_context,
        const std::vector<cl::Device>& devices);
    void SetArgument(std::uint32_t argIndex, void const* data, size_t size);
    const cl::Kernel& GetKernel() const { return m_Kernel; }

private:
    cl::Kernel  m_Kernel;
    cl::Program m_Program;

};

#endif // CL_CONTEXT_HPP
