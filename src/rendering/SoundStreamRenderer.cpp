#include "SoundStreamRenderer.h"
#include "SoundBufferManager.h"
#include "SoundSourceManager.h"
#include "SoundSource.h"
#include "SoundBuffer.h"
#include "SoundStream.h"
#include "OpenAL.h"
#include "EngineSettings.h"
#include "FixedArray.h"


sStreamBinding::sStreamBinding(const vRenderable *data, cSoundSource *source)
{
    m_Stream = data;
    m_Source = source;
    m_Marked = false;
};

sStreamBinding::sStreamBinding()
{
    m_Stream = NULL;
    m_Source = NULL;
    m_Marked = false;
};

cSoundStreamRenderer::cSoundStreamRenderer()
{
};

cSoundStreamRenderer::~cSoundStreamRenderer()
{
    Release();
};

bool cSoundStreamRenderer::Initialize(cSoundBufferManager *bufferman, cSoundSourceManager *srcman)
{
    if(!bufferman || !srcman)
        return false;

    if(IsValid() && bufferman == m_BufferManager && srcman == m_SourceManager)
        return true;

    Release();

    m_BufferManager = bufferman;
    m_SourceManager = srcman;

    return IsValid();
};

bool cSoundStreamRenderer::IsValid() const
{
    // TODO iManagerBase::IsValid()
    bool man_valid = m_BufferManager ? true : false;
    man_valid &= m_SourceManager ? true : false;
    
    return man_valid & OpenAL::Get().IsInitialized();
};

void cSoundStreamRenderer::OnFrame(float dt)
{
    if(!IsValid())
        return;

    UpdateStreams();

    UnmarkAllBindings();
};

void cSoundStreamRenderer::Render(const vRenderable &object)
{
    if(!IsValid())
        return;

    if(!object.IsValid())
        return;

    if(object.GetRenderableType() != ERenderableType::SoundStream)
        return;

    if(!object.GetRenderingProperties())
        return;

    int binding = FindStreamBinding(object);
    if(binding < 0)
        binding = CreateBinding(object);   
    
    if(binding >= 0)
        m_DataBindings[binding].m_Marked = true;
};

void cSoundStreamRenderer::Release()
{
    ClearDataBindings();

    ReleaseSources();
    ReleaseBuffers();
    
    m_BufferManager = NULL;
    m_SourceManager = NULL;
};

void cSoundStreamRenderer::UpdateStreams()
{
    RemoveUnmarkedBindings();

    for(unsigned int i = 0; i < m_DataBindings.size(); ++i)
    {
        sStreamBinding &str = m_DataBindings[i];

        if(!str.m_Source || !str.m_Stream)
        {
            ClearBinding(str);
            continue;
        }

        vSoundStreamProperties *props = dynamic_cast<vSoundStreamProperties*>(str.m_Stream->GetRenderingProperties());
        if(!props)
            continue;
    
        switch(props->GetWantedState())
        {
        case ESourceState::Playing: PlayStream(str); break;
        case ESourceState::Stopped: StopStream(str); break;
        case ESourceState::Paused:  PauseStream(str); break;
        }
    }


    for(unsigned int i = 0; i < m_DataBindings.size(); ++i)
    {
        sStreamBinding &str = m_DataBindings[i];
        vSoundStreamProperties *props = dynamic_cast<vSoundStreamProperties*>(str.m_Stream->GetRenderingProperties());

        props->SetState(OpenAL::Get().GetSourceState(str.m_Source->Get()));
    }
};

int cSoundStreamRenderer::FindStreamBinding(const vRenderable &stream)
{
    for(unsigned int i = 0; i < m_DataBindings.size(); ++i)
    {
        if(m_DataBindings[i].m_Stream == &stream)
            return i;
    }

    return -1;
};

void cSoundStreamRenderer::StopStream(sStreamBinding &binding)
{
    if(!binding.m_Source)
        return;

    alSourceStop(binding.m_Source->Get());

    int queued = OpenAL::Get().GetProcessedBuffersCount(binding.m_Source->Get());
    
    for(int i = 0; i < queued; ++i)
        OpenAL::Get().PopBufferQueue(binding.m_Source->Get());
       
    binding.m_Buffers.FreeAll();
};

void cSoundStreamRenderer::PauseStream(sStreamBinding &binding)
{
    if(!binding.m_Source)
        return;

    alSourcePause(binding.m_Source->Get());
};

void cSoundStreamRenderer::PlayStream(sStreamBinding &binding)
{
    if(!binding.m_Source || !binding.m_Stream)
        return;

    vSoundStreamProperties *props = dynamic_cast<vSoundStreamProperties*>(binding.m_Stream->GetRenderingProperties());
    if(!props)
        return;

    sSourceProperties source_props;
    source_props.m_Volume = props->GetVolume();

    ALuint sid = binding.m_Source->Get();

    OpenAL::Get().SetSourceProperties(sid, source_props);

    for(unsigned int i = 0; i < props->GetQueuedChunksCount();)
    {
        cSoundBuffer *buf = NULL;
        m_Buffers.Get(buf);
        if(!buf)
            break;

        cFixedArray<char> *array = props->PopQueue();
        if(!array)
            continue;

        OpenAL::Get().FillBufferData(buf->Get(), array, props->GetFormat(), props->GetFrequency());
        OpenAL::Get().PushBufferQueue(sid, buf->Get());

        ++i;
    }

    if(OpenAL::Get().GetSourceState(sid) != ESourceState::Playing)
        alSourcePlay(sid);
};

void cSoundStreamRenderer::ClearDataBindings()
{
    for(unsigned int i = 0; i < m_DataBindings.size(); ++i)
        ClearBinding(m_DataBindings[i]);

    m_DataBindings.clear();
};

void cSoundStreamRenderer::RemoveUnmarkedBindings()
{
    for(unsigned int i = 0; i < m_DataBindings.size();)
    {
        if(!m_DataBindings[i].m_Marked)
        {
            ClearBinding(m_DataBindings[i]);
            m_DataBindings[i] = m_DataBindings.back();
            m_DataBindings.pop_back();
        }
        else
            ++i;
    }
};

void cSoundStreamRenderer::ClearBinding(sStreamBinding &bnd)
{
    StopStream(bnd);

    m_Sources.Return(bnd.m_Source);

    bnd.m_Source = NULL;

    bnd.m_Stream = NULL;

    bnd.m_Buffers.FreeAll();
    while(bnd.m_Buffers.HasFree())
    {
        cSoundBuffer *buf = NULL;
        bnd.m_Buffers.Get(buf);
        m_Buffers.Return(buf);
    }
    bnd.m_Buffers.Clear();

    bnd.m_Marked = false;
}

void cSoundStreamRenderer::UnmarkAllBindings()
{
    for(unsigned int i = 0; i < m_DataBindings.size(); ++i)
        m_DataBindings[i].m_Marked = false;
}

void cSoundStreamRenderer::ReleaseSources()
{
    m_Sources.FreeAll();
    while(m_Sources.HasFree())
    {
        cSoundSource *src = NULL;
        m_Sources.Get(src);

        if(m_SourceManager)
            m_SourceManager->ReleaseResource(src);
        
        if(src)
            src->Release();
    }

    m_Sources.Clear();
};

void cSoundStreamRenderer::ReleaseBuffers()
{
    m_Buffers.FreeAll();
    while(m_Buffers.HasFree())
    {
        cSoundBuffer *buf = NULL;
        m_Buffers.Get(buf);

        if(m_BufferManager)
            m_BufferManager->ReleaseResource(buf);
        
        if(buf)
            buf->Release();
    }

    m_Buffers.Clear();
};


void cSoundStreamRenderer::TryCreateBuffers(unsigned int count)
{
    if(!IsValid())
        return;

    if(m_Buffers.HasFree())
        return;

    for(unsigned int i = 0; i < count; ++i)
    {
        cSoundBuffer *buf = m_BufferManager->Get();
        if(!buf)
            return;

        m_Buffers.Add(buf);
    }
};

void cSoundStreamRenderer::TryCreateSources(unsigned int count)
{
    if(!IsValid())
        return;

    if(m_Sources.HasFree())
        return;

    for(unsigned int i = 0; i < count; ++i)
    {
        cSoundSource *src = m_SourceManager->Get();
        if(!src)
            return;

        m_Sources.Add(src);
    }
};