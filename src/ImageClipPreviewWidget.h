#pragma once

#include <QWidget>
#include <QImage>
#include <QString>
#include <QRectF>
#include <optional>

class QFrame;
class QComboBox;
class QPushButton;

namespace vlip {

class CropOverlay;

class ImageClipPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ImageClipPreviewWidget(QWidget* parent = nullptr);
    void setImage(const QString& path);
    // Persisted crop applied to the displayed image (normalized 0..1).
    void setCrop(const std::optional<QRectF>& crop);
    // Project canvas dimensions — used to label and apply the "Project"
    // aspect-ratio preset in crop mode. Safe to call any time.
    void setProjectCanvas(int width, int height);
    void clear();

    // Enter the interactive crop UI. Initial selection = current crop or
    // the full image; aspect mode resets to Free.
    void enterCropMode();
    bool inCropMode() const { return m_cropping; }

signals:
    // Emitted when the user clicks Apply or Reset.
    void cropApplied(const std::optional<QRectF>& normalized);
    void cropModeExited();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QRect imagePaintRect() const;
    void buildCropUi();
    void positionCropUi();
    void exitCropMode();

    QString m_path;
    QImage m_orig;
    std::optional<QRectF> m_crop;
    int m_projectW = 1920;
    int m_projectH = 1080;

    bool m_cropping = false;

    // Crop UI (built once, lazily).
    CropOverlay* m_overlay = nullptr;
    QFrame* m_cropBar = nullptr;
    QComboBox* m_aspect = nullptr;
    QPushButton* m_btnApply = nullptr;
    QPushButton* m_btnReset = nullptr;
    QPushButton* m_btnCancel = nullptr;
};

} // namespace vlip
