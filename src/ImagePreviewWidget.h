#pragma once

#include <QWidget>
#include <QImage>
#include <QString>

namespace vlip {

class ImagePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ImagePreviewWidget(QWidget* parent = nullptr);
    void setImage(const QString& path);
    void clear();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QString m_path;
    QImage m_orig;
};

} // namespace vlip
