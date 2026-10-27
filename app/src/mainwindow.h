#pragma once

#include "library.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private slots:
    void rescan();
    void refill();
    void playSelected();
    void addFolder();
    void showSelectedInFolder();
    void showContextMenu(const QPoint &pos);

private:
    enum class Category { All, Songs, Videos };

    Category currentCategory() const;
    const Playlist *selectedPlaylist() const;
    QString findMpv() const;

    Library m_library;
    QListWidget *m_categories = nullptr;
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_lists = nullptr;
    QLabel *m_status = nullptr;
};
