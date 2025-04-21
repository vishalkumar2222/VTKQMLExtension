#include "VTKRendererItem.h"

#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRenderNode>
#include <QtQuick/QSGRendererInterface>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTextureProvider>

#include <QtGui/QOpenGLContext>
#include <QtGui/QScreen>

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QQueue>

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkOpenGLFramebufferObject.h>
#include <vtkTextureObject.h>
#include <vtkOpenGLState.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkConeSource.h>
#include <vtkPolyDataMapper.h>

#include <QVTKInteractor.h>
#include <QVTKInteractorAdapter.h>
#include <QVTKRenderWindowAdapter.h>

QT_WARNING_DISABLE_GCC("-Wshadow")
QT_WARNING_DISABLE_CLANG("-Wshadow")
QT_WARNING_DISABLE_MSVC(4458)

#define NO_TOUCH

void VTKRendererItem::setGraphicsApi()
{
    QSurfaceFormat fmt = QVTKRenderWindowAdapter::defaultFormat(false);
    fmt.setAlphaBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
}

VTKRendererItem::vtkUserData VTKRendererItem::initializeVTK(vtkRenderWindow *renderWindow){
    vtkNew<vtkConeSource> cone;

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(cone->GetOutputPort());

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);

    vtkNew<vtkRenderer> renderer;
    renderer->AddActor(actor);
    renderer->ResetCamera();
    renderer->SetBackground(1.0, 1.0, 1.0);
    renderWindow->AddRenderer(renderer);
    renderWindow->SetMultiSamples(16);
    return nullptr;
}

//-------------------------------------------------------------------------------------------------

class QSGVtkObjectNode;

class VTKRendererItemPrivate
{
public:
    VTKRendererItemPrivate(VTKRendererItem* ptr)
        : q_ptr(ptr)
    {
    }

    QQueue<std::function<void(vtkRenderWindow*, VTKRendererItem::vtkUserData)>> asyncDispatch;

    QVTKInteractorAdapter qt2vtkInteractorAdapter;
    bool scheduleRender = false;

    mutable QSGVtkObjectNode* node = nullptr;

private:
    Q_DISABLE_COPY(VTKRendererItemPrivate)
    Q_DECLARE_PUBLIC(VTKRendererItem)
    VTKRendererItem* const q_ptr;
};

namespace
{
bool checkGraphicsApi(QQuickWindow* window)
{
    auto api = window->rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::OpenGL
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        && api != QSGRendererInterface::OpenGLRhi
#endif
        )
    {
        qFatal(R"***(Error: QtQuick scenegraph is using an unsupported graphics API: %d.
Set the QSG_INFO environment variable to get more information.
Use QQuickVTKItem::setupGraphicsApi() to set the OpenGLRhi backend.)***",
               api);
    }
    return true;
}
}

VTKRendererItem::VTKRendererItem(QQuickItem *parent)
    :QQuickItem(parent), _d_ptr(new VTKRendererItemPrivate(this)){

    setAcceptHoverEvents(true);

#ifndef NO_TOUCH
    setAcceptTouchEvents(true);
#endif
    setAcceptedMouseButtons(Qt::AllButtons);

    setFlag(QQuickItem::ItemIsFocusScope);
    setFlag(QQuickItem::ItemHasContents);
}

VTKRendererItem::~VTKRendererItem() = default;


void VTKRendererItem::dispatch_async(std::function<void(vtkRenderWindow*, vtkUserData)> f)
{
    Q_D(VTKRendererItem);

    d->asyncDispatch.append(f);

    update();
}

class QSGVtkObjectNode
    : public QSGTextureProvider
    , public QSGSimpleTextureNode
{
    Q_OBJECT
public:
    QSGVtkObjectNode() { qsgnode_set_description(this, QStringLiteral("vtknode")); }

    ~QSGVtkObjectNode() override
    {
        if (m_item)
            m_item->destroyingVTK(vtkWindow, vtkUserData);

        delete QSGVtkObjectNode::texture();

        // Cleanup the VTK window resources
        vtkWindow->GetRenderers()->InitTraversal();
        while (auto renderer = vtkWindow->GetRenderers()->GetNextItem())
            renderer->ReleaseGraphicsResources(vtkWindow);
        vtkWindow->ReleaseGraphicsResources(vtkWindow);
        vtkWindow = nullptr;

        // Cleanup the User Data
        vtkUserData = nullptr;
    }

    QSGTexture* texture() const override { return QSGSimpleTextureNode::texture(); }

    void initialize(VTKRendererItem* item)
    {
        // Create and initialize the vtkWindow
        vtkWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
        vtkWindow->SetMultiSamples(0);
        vtkWindow->SetReadyForRendering(false);
        vtkWindow->SetFrameBlitModeToNoBlit();
        auto loadFunc = [](void*, const char* name) -> vtkOpenGLRenderWindow::VTKOpenGLAPIProc
        {
            if (auto context = QOpenGLContext::currentContext())
            {
                if (auto* symbol = context->getProcAddress(name))
                {
                    return symbol;
                }
            }
            return nullptr;
        };
        vtkWindow->SetOpenGLSymbolLoader(loadFunc, nullptr);
        vtkNew<QVTKInteractor> iren;
        iren->SetRenderWindow(vtkWindow);
        vtkNew<vtkInteractorStyleTrackballCamera> style;
        iren->SetInteractorStyle(style);
        vtkUserData = item->initializeVTK(vtkWindow);
        auto* ia = vtkWindow->GetInteractor();
        if (ia && !QVTKInteractor::SafeDownCast(ia))
        {
            qWarning().nospace() << "QQuickVTKItem.cpp:" << __LINE__
                                 << ", YIKES!! Only QVTKInteractor is supported";
            return;
        }
        vtkWindow->SetReadyForRendering(false);
        vtkWindow->GetInteractor()->Initialize();
        vtkWindow->SetMapped(true);
        vtkWindow->SetIsCurrent(true);
        vtkWindow->SetForceMaximumHardwareLineWidth(1);
        vtkWindow->SetOwnContext(false);
        vtkWindow->OpenGLInitContext();
    }
    void scheduleRender()
    {
        // Update only if we have a window and a render is not already queued.
        if (m_window && !m_renderPending)
        {
            m_renderPending = true;
            m_window->update();
        }
    }

public Q_SLOTS: // NOLINT(readability-redundant-access-specifiers)
    void render()
    {
        if (m_renderPending)
        {
            const bool needsWrap = m_window &&
                                   QSGRendererInterface::isApiRhiBased(m_window->rendererInterface()->graphicsApi());
            if (needsWrap)
                m_window->beginExternalCommands();

            // Render VTK into it's framebuffer
            auto ostate = vtkWindow->GetState();
            ostate->Reset();
            ostate->Push();
            ostate->vtkglDepthFunc(GL_LEQUAL); // note: By default, Qt sets the depth function to GL_LESS
            // but VTK expects GL_LEQUAL
            vtkWindow->SetReadyForRendering(true);
            vtkWindow->GetInteractor()->ProcessEvents();
            vtkWindow->GetInteractor()->Render();
            vtkWindow->SetReadyForRendering(false);
            ostate->Pop();

            if (needsWrap)
                m_window->endExternalCommands();

            m_renderPending = false;
            markDirty(QSGNode::DirtyMaterial);
            Q_EMIT textureChanged();
        }
    }

    void handleScreenChange(QScreen*)
    {
        if (!m_window || !m_item)
            return;

        if (m_window->effectiveDevicePixelRatio() != m_devicePixelRatio)
        {

            m_item->update();
        }
    }

private:
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> vtkWindow;
    vtkSmartPointer<vtkObject> vtkUserData;
    bool m_renderPending = false;

    QPointer<QQuickWindow> m_window;
    QPointer<VTKRendererItem> m_item;
    qreal m_devicePixelRatio = 0;
    QSizeF m_size;
    friend class VTKRendererItem;
};


QSGNode *VTKRendererItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    auto* n = static_cast<QSGVtkObjectNode*>(node);

    // Don't create the node if our size is invalid
    if (!n && (width() <= 0 || height() <= 0))
        return nullptr;

    Q_D(VTKRendererItem);

    // Create the QSGRenderNode
    if (!n)
    {
        if (!checkGraphicsApi(window()))
            return nullptr;
        if (!d->node)
            d->node = new QSGVtkObjectNode;
        n = d->node;
    }

    // Initialize the QSGRenderNode
    if (!n->m_item)
    {
        n->initialize(this);
        n->m_window = window();
        n->m_item = this;
        connect(window(), &QQuickWindow::beforeRendering, n, &QSGVtkObjectNode::render);
        connect(window(), &QQuickWindow::screenChanged, n, &QSGVtkObjectNode::handleScreenChange);
    }

    // Watch for size changes
    auto size = QSizeF(width(), height());
    n->m_devicePixelRatio = window()->devicePixelRatio();
    d->qt2vtkInteractorAdapter.SetDevicePixelRatio(n->m_devicePixelRatio);
    auto sz = size * n->m_devicePixelRatio;
    bool dirtySize = sz != n->m_size;
    if (dirtySize)
    {
        n->vtkWindow->SetSize(sz.width(), sz.height());
        n->vtkWindow->GetInteractor()->SetSize(n->vtkWindow->GetSize());
        delete n->texture();
        n->m_size = sz;
    }

    // Dispatch commands to VTK
    if (!d->asyncDispatch.empty())
    {
        n->scheduleRender();

        n->vtkWindow->SetReadyForRendering(true);
        while (!d->asyncDispatch.empty())
            d->asyncDispatch.dequeue()(n->vtkWindow, n->vtkUserData);
        n->vtkWindow->SetReadyForRendering(false);
    }

    // Whenever the size changes we need to get a new FBO from VTK so we need to render right now
    // (with the gui-thread blocked) for this one frame.
    if (dirtySize)
    {
        n->scheduleRender();
        n->render();
        auto fb = n->vtkWindow->GetDisplayFramebuffer();
        if (fb && fb->GetNumberOfColorAttachments() > 0)
        {
            GLuint texId = fb->GetColorAttachmentAsTextureObject(0)->GetHandle();
            auto* texture = QNativeInterface::QSGOpenGLTexture::fromNative(
                texId, window(), sz.toSize(), QQuickWindow::TextureIsOpaque);
            n->setTexture(texture);
        }
        else if (!fb)
            qFatal("%s %d %s", "VTKRendererItem.cpp:", __LINE__,
                   ", YIKES!!, Render() didn't create a FrameBuffer!?");
        else
            qFatal("%s %d %s", "VTKRendererItem.cpp:", __LINE__,
                   ", YIKES!!, Render() didn't create any ColorBufferAttachements in its FrameBuffer!?");
    }

    n->setTextureCoordinatesTransform(QSGSimpleTextureNode::MirrorVertically);
    n->setFiltering(smooth() ? QSGTexture::Linear : QSGTexture::Nearest);
    n->setRect(0, 0, width(), height());

    if (d->scheduleRender)
    {
        n->scheduleRender();
        d->scheduleRender = false;
    }

    return n;
}

void VTKRendererItem::scheduleRender()
{
    Q_D(VTKRendererItem);

    d->scheduleRender = true;
    update();
}

bool VTKRendererItem::isTextureProvider() const
{
    return true;
}

QSGTextureProvider* VTKRendererItem::textureProvider() const
{
    // When Item::layer::enabled == true, QQuickItem will be a texture provider.
    // In this case we should prefer to return the layer rather than the VTK texture.
    if (QQuickItem::isTextureProvider())
        return QQuickItem::textureProvider();

    if (!checkGraphicsApi(window()))
        return nullptr;

    Q_D(const VTKRendererItem);

    if (!d->node)
        d->node = new QSGVtkObjectNode;

    return d->node;
}

void VTKRendererItem::releaseResources()
{
    // When release resources is called on the GUI thread, we only need to
    // forget about the node. Since it is the node we returned from updatePaintNode
    // it will be managed by the scene graph.
    Q_D(VTKRendererItem);
    d->node = nullptr;
}

void VTKRendererItem::invalidateSceneGraph()
{
    Q_D(VTKRendererItem);
    d->node = nullptr;
}

bool VTKRendererItem::event(QEvent* ev)
{
    Q_D(VTKRendererItem);

    if (!ev)
        return false;

    auto e = ev->clone();
    dispatch_async(
        [d, e](vtkRenderWindow* vtkWindow, vtkUserData) mutable
        {
            d->qt2vtkInteractorAdapter.ProcessEvent(e, vtkWindow->GetInteractor());
            delete e;
        });

    ev->accept();

    return true;
}

#include "VTKRendererItem.moc"
