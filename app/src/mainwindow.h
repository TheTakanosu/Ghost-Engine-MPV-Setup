#pragma once

#include "library.h"
#include "resolver.h"
#include "thumbnails.h"
#include "youtube.h"

#include <QMainWindow>

class CoverView;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private slots:
    void rescan();
    void refill();
    void showSelection();
    void play(bool shuffle = false);
    void addFolder();
    void showSelectedInFolder();
    void showContextMenu(const QPoint &pos);

private:
    enum class Category { All, Songs, Videos };

    const Playlist *selectedPlaylist() const;
    QString coverId(const Playlist &p);
    QString findMpv() const;

    Library m_library;
    ThumbnailCache m_covers;
    youtube::Matches m_matches;
    CoverResolver m_resolver;
    Category m_category = Category::All;

    QButtonGroup *m_chips = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_cards = nullptr;
    CoverView *m_hero = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_status = nullptr;
};
