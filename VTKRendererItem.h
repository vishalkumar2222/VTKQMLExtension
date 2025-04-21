#ifndef VTKRENDERERITEM_H
#define VTKRENDERERITEM_H

#include <QQuickItem>
#include <QtCore/QScopedPointer>
#include <vtkSmartPointer.h>
#include <functional>

class vtkRenderWindow;
class vtkObject;

class VTKRendererItemPrivate;
class VTKRendererItem : public QQuickItem
{
    Q_OBJECT
public:
    explicit VTKRendererItem(QQuickItem *parent = nullptr);

    ~VTKRendererItem() override;

    using vtkUserData = vtkSmartPointer<vtkObject>;

    static void setGraphicsApi();

    virtual vtkUserData initializeVTK(vtkRenderWindow* renderWindow);

    virtual void destroyingVTK(vtkRenderWindow* renderWindow, vtkUserData userData)
    {
        Q_UNUSED(renderWindow);
        Q_UNUSED(userData);
    }

    void dispatch_async(std::function<void(vtkRenderWindow* renderWindow, vtkUserData userData)> f);

    void scheduleRender();

private:
    bool event(QEvent* event) override;

    QSGNode* updatePaintNode(QSGNode*node, UpdatePaintNodeData*) override;
    bool isTextureProvider() const override;
    QSGTextureProvider* textureProvider() const override;
    void releaseResources() override;

private Q_SLOTS:
    void invalidateSceneGraph();

private: // NOLINT(readability-redundant-access-specifiers)
    Q_DISABLE_COPY(VTKRendererItem)
    Q_DECLARE_PRIVATE_D(_d_ptr, VTKRendererItem)
    QScopedPointer<VTKRendererItemPrivate> _d_ptr;

};

#endif // VTKRENDERERITEM_H
