#pragma once

#include <QWidget>

class QSpinBox;
class QDoubleSpinBox;

namespace vlip {

class MainWindow;

class DefaultsPane : public QWidget {
    Q_OBJECT
public:
    explicit DefaultsPane(MainWindow* mw, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    MainWindow* m_mw;
    QSpinBox *m_w, *m_h, *m_fps;
    QDoubleSpinBox *m_imgDur;
    QDoubleSpinBox *m_vidTrimStart, *m_vidTrimEnd;
    QDoubleSpinBox *m_transition;
    bool m_suspend = false;
};

} // namespace vlip
