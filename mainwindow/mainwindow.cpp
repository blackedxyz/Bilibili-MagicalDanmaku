#include <zlib.h>
#include <QListView>
#include <QMovie>
#include <QClipboard>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QDataStream>
#include <QTableWidget>
#include <QSqlQuery>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "videolyricscreator.h"
#include "roomstatusdialog.h"
#include "RoundedAnimationLabel.h"
#include "catchyouwidget.h"
#include "qrcodelogindialog.h"
#include "fileutil.h"
#include "stringutil.h"
#include "escape_dialog/escapedialog.h"
#include "guardonlinedialog.h"
#include "watercirclebutton.h"
#include "variantviewer.h"
#include "csvviewer.h"
#include "buyvipdialog.h"
#include "warmwishutil.h"
#include "httpuploader.h"
#include "third_party/qss_editor/qsseditdialog.h"
#include "third_party/calculator/calculator_util.h"
#ifdef Q_OS_WIN
#include "widgets/windowshwnd.h"
#endif
#include "tx_nlp.h"
#include "conditionutil.h"
#include "order_player/roundedpixmaplabel.h"
#include "string_distance_util.h"
#include "bili_api_util.h"

TxNlp* TxNlp::txNlp = nullptr;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      NetInterface(this),
      ui(new Ui::MainWindow),
      sqlService(this)
{
    ui->setupUi(this);

    initView();
    initStyle();

    // 路径
    initPath();
    initObjects();
    readConfig();
    initEvent();
    initLiveOpenService();
    initDbService();

#ifdef Q_OS_ANDROID
    ui->sideBarWidget->hide();
    ui->verticalLayout->addWidget(ui->robotInfoWidget);
    ui->droplight->hide();
    ui->guardInfoWidget->hide();
    ui->roomPage->layout()->setMargin(0);
    ui->danmakuPage->layout()->setMargin(0);
    ui->thankPage->layout()->setMargin(0);
    ui->musicPage->layout()->setMargin(0);
    ui->extensionPage->layout()->setMargin(0);
    ui->preferencePage->layout()->setMargin(0);
#endif

    // 彩蛋
    warmWish = WarmWishUtil::getWarmWish(":/documents/warm_wish");
    if (!warmWish.isEmpty())
    {
        ui->roomNameLabel->setText(warmWish);

        QTimer::singleShot(500 * warmWish.length(), [=]{
            ui->roomNameLabel->setText(ac->roomTitle);
        });
    }

    QTimer::singleShot(0, [=]{
        triggerCmdEvent("START_UP", LiveDanmaku(), true);
    });

#ifdef Q_OS_WIN
    // 检测VC2015
    QTimer::singleShot(1000, [=]{
        if (hasInstallVC2015())
            return ;

        // 提示下载安装
        auto notify = new NotificationEntry("", "下载VC运行库", "系统缺少必须的VC2015");
        connect(notify, &NotificationEntry::signalCardClicked, this, [=]{
            QDesktopServices::openUrl(QUrl("https://aka.ms/vs/15/release/vc_redist.x64.exe"));
        });
        tip_box->createTipCard(notify);
    });
#endif

    // 检查更新
    syncTimer->start((qrand() % 10 + 10) * 1000);

    // ========== 测试 ==========
}

void MainWindow::initView()
{
    QApplication::setQuitOnLastWindowClosed(false);
    connect(qApp, &QApplication::paletteChanged, this, [=](const QPalette& pa){
        ui->tabWidget->setPalette(pa);
    });
    ui->menubar->setStyleSheet("QMenuBar:item{background:transparent;}QMenuBar{background:transparent;}");

    sideButtonList = { ui->roomPageButton,
                  ui->danmakuPageButton,
                  ui->thankPageButton,
                  ui->musicPageButton,
                  ui->extensionPageButton,
                  ui->preferencePageButton,
                  ui->forumButton
                };
    for (int i = 0; i < sideButtonList.size(); i++)
    {
        auto button = sideButtonList.at(i);
        button->setSquareSize();
        button->setFixedForePos();
        button->setFixedSize(QSize(rt->widgetSizeL, rt->widgetSizeL));
        button->setRadius(rt->fluentRadius, rt->fluentRadius);
        button->setIconPaddingProper(0.23);
//        button->setChokingProp(0.08);

        if (i == sideButtonList.size() - 1) // 最后面的不是切换页面，不设置
            continue;

        connect(button, &InteractiveButtonBase::clicked, this, [=]{
            int prevIndex = ui->stackedWidget->currentIndex();
            if (prevIndex == i) // 同一个索引，不用重复切换
                return ;

            ui->stackedWidget->setCurrentIndex(sideButtonList.indexOf(button));

            adjustPageSize(i);
            switchPageAnimation(i);

            // 当前项
            us->setValue("mainwindow/stackIndex", i);
            foreach (auto btn, sideButtonList)
            {
                btn->setNormalColor(Qt::transparent);
            }
            sideButtonList.at(i)->setNormalColor(themeSbg);
        });
    }

    roomCoverLabel = new QLabel(ui->roomInfoMainWidget);

    // 房号位置
    roomIdBgWidget = new QWidget(this->centralWidget());
    roomSelectorBtn = new WaterCircleButton(QIcon(":/icons/account_link"), ui->roomIdEdit);
    roomIdBgWidget->setObjectName("roomIdBgWidget");
    ui->roomIdEdit->setFixedSize(ui->roomIdEdit->size());
    roomSelectorBtn->setGeometry(ui->roomIdEdit->width() - ui->roomIdEdit->height(), 0, ui->roomIdEdit->height(), ui->roomIdEdit->height());
    ui->roomIdSpacingWidget->setMinimumWidth(ui->roomIdEdit->width() + ui->roomIdEdit->height());
    QHBoxLayout* ril = new QHBoxLayout(roomIdBgWidget);
    ril->addWidget(ui->roomIdEdit);
    ril->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui->roomIdSpacingWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ui->upLevelLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->robotNameButton->setRadius(rt->fluentRadius);
    ui->robotNameButton->setTextDynamicSize();
    ui->robotNameButton->setFontSize(12);
    ui->robotNameButton->setFixedForePos();
    ui->robotNameButton->setPaddings(6);
    ui->liveStatusButton->setTextDynamicSize(true);
    ui->liveStatusButton->setText("");
    ui->liveStatusButton->setRadius(rt->fluentRadius);

    // 隐藏用不到的工具
    ui->pushNextCmdButton->hide();
    ui->timerPushCmdCheck->hide();
    ui->timerPushCmdSpin->hide();
    ui->closeTransMouseButton->hide();
    ui->pkMelonValButton->hide();
    ui->menubar->hide();
    ui->statusbar->hide();

    // 设置属性
    ui->roomDescriptionBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
    QFontMetrics fm(ui->roomDescriptionBrowser->font());
    ui->roomDescriptionBrowser->setMaximumHeight(fm.lineSpacing() * 5);
    ui->guardCountCard->setMinimumSize(ui->guardCountCard->sizeHint());
    ui->hotCountCard->setMinimumSize(ui->hotCountCard->sizeHint());
    int upHeaderSize = ui->upNameLabel->sizeHint().height() + ui->upLevelLabel->sizeHint().height();
    ui->upHeaderLabel->setFixedSize(upHeaderSize * 2, upHeaderSize * 2);
    ui->robotHeaderLabel->setMinimumSize(ui->upHeaderLabel->size());
    ui->robotHeaderLabel->setFixedHeight(ui->upHeaderLabel->height());
    roomSelectorBtn->setSquareSize();
    roomSelectorBtn->setRadius(rt->fluentRadius);
    roomSelectorBtn->setFocusPolicy(Qt::FocusPolicy::ClickFocus);
    ui->tagsButtonGroup->setSelecteable(false);
    ui->robotInfoWidget->setMinimumWidth(upHeaderSize * 2);

    // 避免压缩
    ui->roomInfoMainWidget->setMinimumSize(ui->roomInfoMainWidget->sizeHint());

    // 限制
    ui->roomIdEdit->setValidator(new QRegExpValidator(QRegExp("^\\w+$")));

    // 切换房间
    roomSelectorBtn->show();
    roomSelectorBtn->setCursor(Qt::PointingHandCursor);
    connect(roomSelectorBtn, &InteractiveButtonBase::clicked, this, [=]{
        newFacileMenu;

        menu->addAction(ui->actionAdd_Room_To_List);
        menu->split();

        QStringList list = us->value("custom/rooms", "").toString().split(";", QString::SkipEmptyParts);
        for (int i = 0; i < list.size(); i++)
        {
            QStringList texts = list.at(i).split(",", QString::SkipEmptyParts);
            if (texts.size() < 1)
                continue ;
            QString id = texts.first();
            QString name = texts.size() >= 2 ? texts.at(1) : id;
            menu->addAction(name, [=]{
                ui->roomIdEdit->setText(id);
                on_roomIdEdit_editingFinished();
            });
        }

        menu->exec();
    });

    connect(ui->robotHeaderLabel, &ClickableLabel::clicked, this, [=]{
        if (!ac->cookieUid.isEmpty())
            QDesktopServices::openUrl(QUrl("https://space.bilibili.com/" + ac->cookieUid));
    });

    connect(ui->upHeaderLabel, &ClickableLabel::clicked, this, [=]{
        if (!ac->roomId.isEmpty())
            QDesktopServices::openUrl(QUrl("https://live.bilibili.com/" + ac->roomId));
    });

    connect(ui->upNameLabel, &ClickableLabel::clicked, this, [=]{
        if (!ac->upUid.isEmpty())
            QDesktopServices::openUrl(QUrl("https://space.bilibili.com/" + ac->upUid));
    });

    connect(ui->roomAreaLabel, &ClickableLabel::clicked, this, [=]{
        if (ac->cookieUid == ac->upUid)
        {
            myLiveSelectArea(true);
        }
        else
        {
            if (!ac->areaId.isEmpty())
            {
                QDesktopServices::openUrl(QUrl("https://live.bilibili.com/p/eden/area-tags?areaId=" + ac->areaId + "&parentAreaId=" + ac->parentAreaId));
            }
        }
    });

    connect(ui->roomNameLabel, &ClickableLabel::clicked, this, [=]{
        if (ac->upUid.isEmpty() || ac->upUid != ac->cookieUid)
            return ;

       myLiveSetTitle();
    });

    connect(ui->battleRankIconLabel, &ClickableLabel::clicked, this, [=]{
        showPkMenu();
    });

    connect(ui->battleRankNameLabel, &ClickableLabel::clicked, this, [=]{
        showPkMenu();
    });

    connect(ui->winningStreakLabel, &ClickableLabel::clicked, this, [=]{
        showPkMenu();
    });

    // 吊灯
    ui->droplight->setPaddings(12, 12, 2, 2);
    ui->droplight->adjustMinimumSize();
    ui->droplight->setRadius(rt->fluentRadius);

    // 礼物列表
    const int giftImgSize = ui->upHeaderLabel->width(); // 礼物小图片的高度
    int listHeight = giftImgSize + ui->guardCountLabel->height() + ui->guardCountTextLabel->height();
    ui->giftListWidget->setFixedHeight(listHeight);

    // 弹幕设置瀑布流
    ui->showLiveDanmakuWindowButton->setAutoTextColor(false);
    ui->showLiveDanmakuWindowButton->setFontSize(12);
    ui->showLiveDanmakuWindowButton->setPaddings(16, 16, 4, 4);
    ui->showLiveDanmakuWindowButton->setFixedForeSize();
    ui->showLiveDanmakuWindowButton->setBgColor(Qt::white);

    ui->scrollArea->setItemSpacing(24, 24);
    ui->scrollArea->initFixedChildren();
    ui->scrollArea->resizeWidgetsToEqualWidth();
//    ui->scrollArea->setAllowDifferentWidth(true);
//    ui->scrollArea->resizeWidgetsToSizeHint();
    foreach (auto w, ui->scrollArea->getWidgets())
    {
        // 设置样式
        w->setStyleSheet("#" + w->objectName() + "{"
                                                 "background: white;"
                                                 "border: none;"
                                                 "border-radius:" + snum(rt->fluentRadius) + ";"
                                                 "}");

        // 设置阴影
        QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect(w);
        effect->setColor(QColor(63, 63, 63, 32));
        effect->setBlurRadius(12);
        effect->setXOffset(4);
        effect->setYOffset(4);
        w->setGraphicsEffect(effect);
    }

    ui->SendMsgButton->setFixedForeSize();
    ui->SendMsgButton->setRadius(rt->fluentRadius);
    ui->sendMsgMoreButton->setSquareSize();
    ui->sendMsgMoreButton->setRadius(rt->fluentRadius);

    ui->saveEveryGiftButton->setSquareSize();
    ui->saveEveryGiftButton->setRadius(rt->fluentRadius);
    ui->saveEveryGuardButton->setSquareSize();
    ui->saveEveryGuardButton->setRadius(rt->fluentRadius);
    ui->saveMonthGuardButton->setSquareSize();
    ui->saveMonthGuardButton->setRadius(rt->fluentRadius);
    ui->saveDanmakuToFileButton->setSquareSize();
    ui->saveDanmakuToFileButton->setRadius(rt->fluentRadius);
    ui->calculateDailyDataButton->setSquareSize();
    ui->calculateDailyDataButton->setRadius(rt->fluentRadius);
    ui->recordDataButton->setSquareSize();
    ui->recordDataButton->setRadius(rt->fluentRadius);
    ui->ffmpegButton->setSquareSize();
    ui->ffmpegButton->setRadius(rt->fluentRadius);

    // 答谢页面
    thankTabButtons = {
        ui->thankWelcomeTabButton,
        ui->thankGiftTabButton,
        ui->thankAttentionTabButton,
        ui->blockTabButton
    };
    foreach (auto btn, thankTabButtons)
    {
        btn->setAutoTextColor(false);
        btn->setPaddings(18, 18, 0, 0);
    }
    ui->thankTopTabGroup->layout()->activate();
    ui->thankTopTabGroup->setFixedHeight(ui->thankTopTabGroup->sizeHint().height());
    ui->thankTopTabGroup->setStyleSheet("#thankTopTabGroup { background: white; border-radius: " + snum(ui->thankTopTabGroup->height() / 2) + "px; }");

    customVarsButton = new InteractiveButtonBase(QIcon(":/icons/settings"), ui->thankPage);
    customVarsButton->setRadius(rt->fluentRadius);
    customVarsButton->setSquareSize();
    customVarsButton->setCursor(Qt::PointingHandCursor);
    int tabBarHeight = ui->thankTopTabGroup->height();
    customVarsButton->setFixedSize(tabBarHeight, tabBarHeight);
    customVarsButton->show();
    connect(customVarsButton, &InteractiveButtonBase::clicked, this, [=]{
        newFacileMenu;
        menu->addAction(ui->actionCustom_Variant);
        menu->addAction(ui->actionReplace_Variant);
        menu->split()->addAction(ui->actionLocal_Mode);
        menu->addAction(ui->actionDebug_Mode);
        menu->addAction(ui->actionLast_Candidate);
        menu->exec();
    });

    // 禁言
    ui->eternalBlockListButton->adjustMinimumSize();
    ui->eternalBlockListButton->setBorderColor(Qt::lightGray);
    ui->eternalBlockListButton->setBgColor(Qt::white);
    ui->eternalBlockListButton->setRadius(rt->fluentRadius);
    ui->eternalBlockListButton->setFixedForePos();

    // 点歌页面
    ui->showOrderPlayerButton->setFixedForeSize(true, 12);
    ui->showOrderPlayerButton->setAutoTextColor(false);
    ui->showOrderPlayerButton->setBgColor(QColor(180, 166, 211));
    {
        QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect(ui->showOrderPlayerButton);
        effect->setColor(QColor(63, 63, 63, 64));
        effect->setBlurRadius(24);
        effect->setXOffset(4);
        effect->setYOffset(4);
        ui->showOrderPlayerButton->setGraphicsEffect(effect);
    }
    {
        QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect(ui->musicConfigCardWidget);
        effect->setColor(QColor(63, 63, 63, 32));
        effect->setBlurRadius(24);
        effect->setXOffset(2);
        effect->setYOffset(2);
        ui->musicConfigCardWidget->setGraphicsEffect(effect);
    }
    ui->musicImageWidget->setPixmap(QPixmap(":/bg/bg"));
    ui->musicImageWidget->setPart([=](QRect canvas) -> QPainterPath {
        int w = canvas.width(), h = canvas.height();
        QPainterPath path;
        path.addRoundedRect(w / 9, h / 3, w / 3, h * 2 / 3, rt->fluentRadius, rt->fluentRadius);
        path.addRoundedRect(w * 5 / 9, 0, w / 3, h * 2 / 3, rt->fluentRadius, rt->fluentRadius);
        return path;
    });
    musicTitleDecorateWidget = new CustomPaintWidget(ui->musicContainerWidget);
    QFont musicTitleColFont(ui->musicBigTitleLabel->font());
    musicTitleColFont.setPointSize(qMax(musicTitleColFont.pointSize(), 20) * 4 / 3);
    fm = QFontMetrics(musicTitleColFont);
    const QString musicTitleDecorate = "ORDER MUSIC";
    musicTitleDecorateWidget->setFixedSize(fm.height(), fm.horizontalAdvance(musicTitleDecorate));
    musicTitleDecorateWidget->setPaint([=](QRect geom, QPainter* painter) -> void {
        QString text = ui->musicBigTitleLabel->text();
        painter->translate(geom.width(), 0);
        painter->rotate(90);
        painter->setFont(musicTitleColFont);
        painter->setPen(QPen(QColor(128, 128, 128, 32)));
        painter->drawText(QRect(0, 0, geom.height(), geom.width()), Qt::AlignCenter, musicTitleDecorate);
    });
    musicTitleDecorateWidget->lower();
    musicTitleDecorateWidget->stackUnder(ui->musicBigTitleLabel);
    ui->musicBlackListButton->setTextColor(Qt::gray);
    ui->musicBlackListButton->setRadius(rt->fluentRadius);
    ui->musicBlackListButton->adjustMinimumSize();
    ui->musicBlackListButton->setFixedForePos();
    ui->musicConfigButton->setSquareSize();
    ui->musicConfigButton->setFixedForePos();
    ui->musicConfigButton->setRadius(rt->fluentRadius);
    ui->musicConfigButtonSpacingWidget->setFixedWidth(ui->musicConfigButton->width());
    ui->addMusicToLiveButton->setRadius(rt->fluentRadius);
    ui->addMusicToLiveButton->setPaddings(8);
    ui->addMusicToLiveButton->adjustMinimumSize();
    ui->addMusicToLiveButton->setBorderWidth(1);
    ui->addMusicToLiveButton->setBorderColor(Qt::lightGray);
    ui->addMusicToLiveButton->setFixedForePos();

    // 扩展页面
    extensionButton = new InteractiveButtonBase(QIcon(":/icons/settings"), ui->tabWidget);
    extensionButton->setRadius(rt->fluentRadius);
    extensionButton->setSquareSize();
    extensionButton->setCursor(Qt::PointingHandCursor);
    tabBarHeight = ui->tabWidget->tabBar()->height();
    extensionButton->setFixedSize(tabBarHeight, tabBarHeight);
    extensionButton->show();
    connect(extensionButton, &InteractiveButtonBase::clicked, this, [=]{
        newFacileMenu;
        menu->addAction(ui->actionCustom_Variant);
        menu->addAction(ui->actionReplace_Variant);
        menu->split()->addAction(ui->actionPaste_Code);
        menu->addAction(ui->actionGenerate_Default_Code);
        menu->addAction(ui->actionRead_Default_Code);
        menu->split()->addAction(ui->actionLocal_Mode);
        menu->addAction(ui->actionData_Path);
        menu->addAction(ui->actionDebug_Mode);
        menu->addAction(ui->actionLast_Candidate);
        menu->exec();
    });

    appendListItemButton = new AppendButton(ui->tabWidget);
    appendListItemButton->setFixedSize(rt->widgetSizeL, rt->widgetSizeL);
    appendListItemButton->setRadius(rt->widgetSizeL);
    appendListItemButton->setCursor(Qt::PointingHandCursor);
    appendListItemButton->setBgColor(Qt::white);
    {
        QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect(appendListItemButton);
        effect->setColor(QColor(63, 63, 63, 128));
        effect->setBlurRadius(24);
        effect->setXOffset(4);
        effect->setYOffset(4);
        appendListItemButton->setGraphicsEffect(effect);
    }
    connect(appendListItemButton, SIGNAL(clicked()), this, SLOT(addListItemOnCurrentPage()));

    ui->vipExtensionButton->setBgColor(Qt::white);
    ui->vipExtensionButton->setRadius(rt->fluentRadius);

    // 网页
    ui->refreshExtensionListButton->setSquareSize();

#ifdef ZUOQI_ENTRANCE
    // 坐骑
    fakeEntrance = new SingleEntrance(this);
    fakeEntrance->setGeometry(this->rect());
    connect(fakeEntrance, &SingleEntrance::signalRoomIdChanged, this, [=](QString roomId){
        if (roomId == "esc")
        {
            fakeEntrance->deleteLater();
            return ;
        }
        ui->roomIdEdit->setText(roomId);
        on_roomIdEdit_editingFinished();
    });
    connect(fakeEntrance, &SingleEntrance::signalLogin, this, [=]{
        on_robotNameButton_clicked();
    });
    this->setWindowTitle("进场坐骑系统");
    if (!ac->identityCode.isEmpty())
        fakeEntrance->setRoomId(ac->identityCode);
#endif

    // 通知
    tip_box = new TipBox(this);
    connect(tip_box, &TipBox::signalCardClicked, [=](NotificationEntry* n){
        qInfo() << "卡片点击：" << n->toString();
    });
    connect(tip_box, &TipBox::signalBtnClicked, [=](NotificationEntry* n){
        qInfo() << "卡片按钮点击：" << n->toString();
    });
}

void MainWindow::initStyle()
{
    ui->roomInfoMainWidget->setStyleSheet("#roomInfoMainWidget\
                        {\
                            background: white;\
                            border-radius: " + snum(rt->fluentRadius) + "px;\
                        }");
    ui->guardCountCard->setStyleSheet("#guardCountWidget\
                        {\
                            background: #f7f7ff;\
                            border: none;\
                            border-radius: " + snum(rt->fluentRadius) + "px;\
                        }");
    ui->hotCountCard->setStyleSheet("#hotCountWidget\
                       {\
                           background: #f7f7ff;\
                           border: none;\
                           border-radius: " + snum(rt->fluentRadius) + "px;\
                                      }");
}

void MainWindow::initPath()
{
    rt->appFileName = QFileInfo(QApplication::applicationFilePath()).baseName();
    if (rt->appFileName == "start" || rt->appFileName == "start.exe")
    {
        rt->asPlugin = true;
        rt->asFreeOnly = true;
    }
    rt->dataPath = QApplication::applicationDirPath() + "/";
#ifdef Q_OS_WIN
    // 如果没有设置通用目录，则选择安装文件夹
    if (QFileInfo(rt->dataPath+"green_version").exists()
            || QFileInfo(rt->dataPath+"green_version.txt").exists())
    {
        // 安装路径，不需要改

    }
    else // 通用文件夹
    {
        rt->dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/";
        // C:/Users/Administrator/AppData/Roaming/神奇弹幕    (未定义ApplicationName时为exe名)
        SOCKET_DEB << "路径：" << rt->dataPath;
    }
#else
    rt->dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/";
    QDir().mkpath(rt->dataPath);
#endif
}

void MainWindow::initRuntime()
{

}

void MainWindow::initObjects()
{
    bool firstOpen = !QFileInfo(rt->dataPath + "settings.ini").exists();
    if (!firstOpen)
    {
        // 备份当前配置：settings和heaps
        ensureDirExist(rt->dataPath + "backup");
        QString ts = QDateTime::currentDateTime().toString("yyyy_MM_dd_hh_mm");
        copyFile(rt->dataPath + "settings.ini", rt->dataPath + "/backup/settings_" + ts + ".ini");
        copyFile(rt->dataPath + "heaps.ini", rt->dataPath + "/backup/heaps_" + ts + ".ini");
    }

    us = new UserSettings(rt->dataPath + "settings.ini");
    heaps = new MySettings(rt->dataPath + "heaps.ini", QSettings::Format::IniFormat);
    extSettings = new MySettings(rt->dataPath + "ext_settings.ini", QSettings::Format::IniFormat);
    robotRecord = new MySettings(rt->dataPath + "robots.ini", QSettings::Format::IniFormat);
    wwwDir = QDir(rt->dataPath + "www");
    liveRoomService = new BiliLiveRoomService(this);
}

/// 读取 settings 中的变量，并进行一系列初始化操作
/// 可能会读取多次，并且随用户命令重复读取
/// 所以里面的所有变量都要做好重复初始化的准备
void MainWindow::readConfig()
{
    // 版本
    rt->appVersion = GetFileVertion(QApplication::applicationFilePath()).trimmed();
    if (rt->appVersion.startsWith("v") || rt->appVersion.startsWith("V"))
            rt->appVersion.replace(0, 1, "");
    QString oldValue = us->value("runtime/appVersion").toString();
    if (rt->appVersion != oldValue)
    {
        upgradeVersionToLastest(oldValue);
        us->setValue("runtime/appVersion", rt->appVersion);
        QTimer::singleShot(1000, [=]{
            QString ch = QApplication::applicationDirPath() + "/CHANGELOG.md";
            if (!oldValue.isEmpty()
                    && ui->showChangelogCheck->isChecked()
                    && isFileExist(ch))
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(ch));
            }
        });
    }
    ui->appNameLabel->setText("神奇弹幕 v" + rt->appVersion);

    // 平台
    rt->livePlatform = (LivePlatform)(us->value("platform/live", 0).toInt());
    us->closeGui = us->value("mainwindow/closeGui", false).toBool();

    // 标签组
    int tabIndex = us->value("mainwindow/tabIndex", 0).toInt();
    if (tabIndex >= 0 && tabIndex < ui->tabWidget->count())
        ui->tabWidget->setCurrentIndex(tabIndex);
    if (tabIndex > 2)
        appendListItemButton->hide();
    else
        appendListItemButton->show();

    // 页面
    int stackIndex = us->value("mainwindow/stackIndex", 0).toInt();
    if (stackIndex >= 0 && stackIndex < ui->stackedWidget->count())
    {
        ui->stackedWidget->setCurrentIndex(stackIndex);
        adjustPageSize(stackIndex);
        QTimer::singleShot(0, [=]{
            switchPageAnimation(stackIndex);
        });
    }

    // 答谢标签
    int thankStackIndex = us->value("mainwindow/thankStackIndex", 0).toInt();
    if (thankStackIndex >= 0 && thankStackIndex < ui->thankStackedWidget->count())
        ui->thankStackedWidget->setCurrentIndex(thankStackIndex);

    // 音乐标签
    int musicStackIndex = us->value("mainwindow/musicStackIndex", 0).toInt();
    if (musicStackIndex >= 0 && musicStackIndex < ui->musicConfigStack->count())
        ui->musicConfigStack->setCurrentIndex(musicStackIndex);

    // 房间号
    ac->roomId = us->value("danmaku/roomId", "").toString();
    if (!ac->roomId.isEmpty())
    {
        ui->roomIdEdit->setText(ac->roomId);
#ifdef ZUOQI_ENTRANCE
        fakeEntrance->setRoomId(ac->identityCode);
#endif
    }
    else // 设置为默认界面
    {
        QTimer::singleShot(0, [=]{
            setRoomCover(QPixmap(":/bg/bg"));
        });
    }

    // 移除间隔
    removeTimer = new QTimer(this);
    removeTimer->setInterval(200);
    connect(removeTimer, SIGNAL(timeout()), this, SLOT(removeTimeoutDanmaku()));
    removeTimer->start();

    int removeIv = us->value("danmaku/removeInterval", 60).toInt();
    ui->removeDanmakuIntervalSpin->setValue(removeIv); // 自动引发改变事件
    this->removeDanmakuInterval = removeIv * 1000;

    removeIv = us->value("danmaku/removeTipInterval", 20).toInt();
    ui->removeDanmakuTipIntervalSpin->setValue(removeIv); // 自动引发改变事件
    this->removeDanmakuTipInterval = removeIv * 1000;

    // 单条弹幕最长长度
    danmuLongest = us->value("danmaku/danmuLongest", 20).toInt();
    ui->danmuLongestSpin->setValue(danmuLongest);
    ui->adjustDanmakuLongestCheck->setChecked(us->value("danmaku/adjustDanmakuLongest", true).toBool());
    robotTotalSendMsg = us->value("danmaku/robotTotalSend", 0).toInt();
    ui->robotSendCountLabel->setText(snum(robotTotalSendMsg));
    ui->robotSendCountLabel->setToolTip("累计发送弹幕 " + snum(robotTotalSendMsg) + " 条");

    // 失败重试
    ui->retryFailedDanmuCheck->setChecked(us->value("danmaku/retryFailedDanmu", true).toBool());

    // 发送队列
    autoMsgTimer = new QTimer(this) ;
    autoMsgTimer->setInterval(1500); // 1.5秒发一次弹幕
    connect(autoMsgTimer, &QTimer::timeout, this, [=]{
        slotSendAutoMsg(true);
    });

    // 点歌
    diangeAutoCopy = us->value("danmaku/diangeAutoCopy", true).toBool();
    ui->DiangeAutoCopyCheck->setChecked(diangeAutoCopy);
    ui->diangeNeedMedalCheck->setChecked(us->value("danmaku/diangeNeedMedal", false).toBool());
    QString defaultDiangeFormat = "^[点點]歌[ :：,，]+(.+)";
    diangeFormatString = us->value("danmaku/diangeFormat", defaultDiangeFormat).toString();
    ui->diangeFormatEdit->setText(diangeFormatString);
    connect(this, SIGNAL(signalNewDanmaku(const LiveDanmaku&)), this, SLOT(slotDiange(const LiveDanmaku&)));
    ui->diangeReplyCheck->setChecked(us->value("danmaku/diangeReply", false).toBool());
    ui->diangeShuaCheck->setChecked(us->value("danmaku/diangeShua", false).toBool());
    ui->autoPauseOuterMusicCheck->setChecked(us->value("danmaku/autoPauseOuterMusic", false).toBool());
    ui->outerMusicKeyEdit->setText(us->value("danmaku/outerMusicPauseKey").toString());
    ui->orderSongsToFileCheck->setChecked(us->value("danmaku/orderSongsToFile", false).toBool());
    ui->orderSongsToFileFormatEdit->setText(us->value("danmaku/orderSongsToFileFormat", "{歌名} - {歌手}").toString());
    ui->playingSongToFileCheck->setChecked(us->value("danmaku/playingSongToFile", false).toBool());
    ui->playingSongToFileFormatEdit->setText(us->value("danmaku/playingSongToFileFormat", "正在播放：{歌名} - {歌手}").toString());
    ui->orderSongsToFileMaxSpin->setValue(us->value("danmaku/orderSongsToFileMax", 9).toInt());
    ui->songLyricsToFileCheck->setChecked(us->value("danmaku/songLyricsToFile", false).toBool());
    ui->songLyricsToFileMaxSpin->setValue(us->value("danmaku/songLyricsToFileMax", 2).toInt());
    ui->orderSongShuaSpin->setValue(us->value("danmaku/diangeShuaCount", 0).toInt());

    // 自动翻译
    bool trans = us->value("danmaku/autoTrans", false).toBool();
    ui->languageAutoTranslateCheck->setChecked(trans);

    // 自动回复
    bool reply = us->value("danmaku/aiReply", false).toBool();
    ui->AIReplyCheck->setChecked(reply);
    ui->AIReplyMsgCheck->setCheckState(static_cast<Qt::CheckState>(us->value("danmaku/aiReplyMsg", 0).toInt()));
    ui->AIReplyMsgCheck->setEnabled(reply);

    // 黑名单管理
    ui->enableBlockCheck->setChecked(us->value("block/enableBlock", false).toBool());
    ui->syncShieldKeywordCheck->setChecked(us->value("block/syncShieldKeyword", false).toBool());

    // 新人提示
    ui->newbieTipCheck->setChecked(us->value("block/newbieTip", false).toBool());

    // 自动禁言
    ui->autoBlockNewbieCheck->setChecked(us->value("block/autoBlockNewbie", false).toBool());
    ui->autoBlockNewbieKeysEdit->setPlainText(us->value("block/autoBlockNewbieKeys").toString());

    ui->autoBlockNewbieNotifyCheck->setChecked(us->value("block/autoBlockNewbieNotify", false).toBool());
    ui->autoBlockNewbieNotifyWordsEdit->setPlainText(us->value("block/autoBlockNewbieNotifyWords").toString());
    ui->autoBlockNewbieNotifyCheck->setEnabled(ui->autoBlockNewbieCheck->isChecked());

    ui->promptBlockNewbieCheck->setChecked(us->value("block/promptBlockNewbie", false).toBool());
    ui->promptBlockNewbieKeysEdit->setPlainText(us->value("block/promptBlockNewbieKeys").toString());

    ui->notOnlyNewbieCheck->setChecked(us->value("block/notOnlyNewbie", false).toBool());
    ui->blockNotOnlyNewbieCheck->setChecked(us->value("block/blockNotOnlyNewbieCheck", false).toBool());

    ui->autoBlockTimeSpin->setValue(us->value("block/autoTime", 1).toInt());

    // 实时弹幕
#ifndef Q_OS_ANDROID
    // 安装因为界面的问题，不主动显示弹幕姬
    if (us->value("danmaku/liveWindow", false).toBool())
         on_actionShow_Live_Danmaku_triggered();
#endif

    // 点歌姬
    if (us->value("danmaku/playerWindow", false).toBool())
        on_actionShow_Order_Player_Window_triggered();
    orderSongBlackList = us->value("music/blackListKeys", "").toString().split(" ", QString::SkipEmptyParts);

    // 录播
    ui->recordCheck->setChecked(us->value("record/enabled", false).toBool());
    ui->recordFormatCheck->setChecked(us->value("record/format", false).toBool());
    rt->ffmpegPath = us->value("record/ffmpegPath").toString();
    int recordSplit = us->value("record/split", 30).toInt();
    ui->recordSplitSpin->setValue(recordSplit);
    recordTimer = new QTimer(this);
    recordTimer->setInterval(recordSplit * 60000); // 默认30分钟断开一次
    connect(recordTimer, &QTimer::timeout, this, [=]{
        if (!recordLoop) // 没有正在录制
            return ;

        qInfo() << "定时停止录播";
        recordLoop->quit(); // 这个就是停止录制了
        // 停止之后，录播会检测是否还需要重新录播
        // 如果是，则继续录
    });

    // 发送弹幕
    ac->browserCookie = us->value("danmaku/browserCookie", "").toString();
    ac->browserData = us->value("danmaku/browserData", "").toString();
    int posl = ac->browserCookie.indexOf("bili_jct=") + 9;
    int posr = ac->browserCookie.indexOf(";", posl);
    if (posr == -1) posr = ac->browserCookie.length();
    ac->csrf_token = ac->browserCookie.mid(posl, posr - posl);
    ac->userCookies = getCookies();
    getCookieAccount();

    // 保存弹幕
    bool saveDanmuToFile = us->value("danmaku/saveDanmakuToFile", false).toBool();
    if (saveDanmuToFile)
        ui->saveDanmakuToFileCheck->setChecked(true);

    // 每日数据
    bool calcDaliy = us->value("live/calculateDaliyData", true).toBool();
    ui->calculateDailyDataCheck->setChecked(calcDaliy);
    if (calcDaliy)
        startCalculateDailyData();



    // PK消息同步
    pkMsgSync = us->value("pk/msgSync", 0).toInt();
    if (pkMsgSync == 0)
        ui->pkMsgSyncCheck->setCheckState(Qt::Unchecked);
    else if (pkMsgSync == 1)
        ui->pkMsgSyncCheck->setCheckState(Qt::PartiallyChecked);
    else if (pkMsgSync == 2)
        ui->pkMsgSyncCheck->setCheckState(Qt::Checked);
    ui->pkMsgSyncCheck->setText(pkMsgSync == 1 ? "PK同步消息(仅视频)" : "PK同步消息");
    ui->pkMsgSyncCheck->setEnabled(pkChuanmenEnable);

    // 判断机器人
    judgeRobot = us->value("danmaku/judgeRobot", 0).toInt();
    ui->judgeRobotCheck->setCheckState((Qt::CheckState)judgeRobot);
    ui->judgeRobotCheck->setText(judgeRobot == 1 ? "机器人判断(仅关注)" : "机器人判断");

    // 本地昵称
    QStringList namePares = us->value("danmaku/localNicknames").toString().split(";", QString::SkipEmptyParts);
    foreach (QString pare, namePares)
    {
        QStringList sl = pare.split("=>");
        if (sl.size() < 2)
            continue;

        us->localNicknames.insert(sl.at(0).toLongLong(), sl.at(1));
    }

    // 礼物别名
    namePares = us->value("danmaku/giftNames").toString().split(";", QString::SkipEmptyParts);
    foreach (QString pare, namePares)
    {
        QStringList sl = pare.split("=>");
        if (sl.size() < 2)
            continue;

        us->giftAlias.insert(sl.at(0).toInt(), sl.at(1));
    }

    // 特别关心
    QStringList usersS = us->value("danmaku/careUsers", "20285041").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersS)
    {
        us->careUsers.append(s.toLongLong());
    }

    // 强提醒
    QStringList usersSN = us->value("danmaku/strongNotifyUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersSN)
    {
        us->strongNotifyUsers.append(s.toLongLong());
    }

    // 不自动欢迎
    QStringList usersNW = us->value("danmaku/notWelcomeUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersNW)
    {
        us->notWelcomeUsers.append(s.toLongLong());
    }

    // 不自动回复
    QStringList usersNR = us->value("danmaku/notReplyUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersNR)
    {
        us->notReplyUsers.append(s.toLongLong());
    }

    // 礼物连击
    ui->giftComboSendCheck->setChecked(us->value("danmaku/giftComboSend", false).toBool());
    ui->giftComboDelaySpin->setValue(us->value("danmaku/giftComboDelay",  5).toInt());
    ui->giftComboTopCheck->setChecked(us->value("danmaku/giftComboTop", false).toBool());
    ui->giftComboMergeCheck->setChecked(us->value("danmaku/giftComboMerge", false).toBool());


    // 仅开播发送
    ui->sendAutoOnlyLiveCheck->setChecked(us->value("danmaku/sendAutoOnlyLive", false).toBool());
    ui->autoDoSignCheck->setChecked(us->value("danmaku/autoDoSign", false).toBool());

    // 勋章升级
    ui->listenMedalUpgradeCheck->setChecked(us->value("danmaku/listenMedalUpgrade", false).toBool());

    // 弹幕次数
    us->danmakuCounts = new QSettings(rt->dataPath+"danmu_count.ini", QSettings::Format::IniFormat);

    // 用户备注
    us->userMarks = new QSettings(rt->dataPath+"user_mark.ini", QSettings::Format::IniFormat);

    // 接收私信
    ui->receivePrivateMsgCheck->setChecked(us->value("privateMsg/enabled", false).toBool());
    ui->processUnreadMsgCheck->setChecked(us->value("privateMsg/processUnread", false).toBool());

    // 过滤器
    enableFilter = us->value("danmaku/enableFilter", enableFilter).toBool();
    ui->enableFilterCheck->setChecked(enableFilter);
    /* filter_musicOrder = settings->value("filter/musicOrder", "").toString();
    filter_musicOrderRe = QRegularExpression(filter_musicOrder);
    filter_danmakuCome = settings->value("filter/danmakuCome", "").toString();
    filter_danmakuGift = settings->value("filter/danmakuGift").toString();*/

    // 编程
    ui->syntacticSugarCheck->setChecked(us->value("programming/syntacticSugar", true).toBool());
    ui->complexCalcCheck->setChecked(us->value("programming/complexCalc", false).toBool());
    ui->stringSimilarCheck->setChecked(us->useStringSimilar = us->value("programming/stringSimilar", false).toBool());
    us->stringSimilarThreshold = us->value("programming/stringSimilarThreshold", 80).toInt();
    us->danmuSimilarJudgeCount = us->value("programming/danmuSimilarJudgeCount", 10).toInt();

    // 状态栏
    statusLabel = new QLabel(this);
    statusLabel->setObjectName("statusLabel");
    this->statusBar()->addWidget(statusLabel, 1);
    statusLabel->setAlignment(Qt::AlignLeft);

    // 托盘
    tray = new QSystemTrayIcon(this);//初始化托盘对象tray
    tray->setIcon(QIcon(QPixmap(":/icons/star")));//设定托盘图标，引号内是自定义的png图片路径
    tray->setToolTip("神奇弹幕");
    QString title="APP Message";
    QString text="神奇弹幕";
//    tray->showMessage(title,text,QSystemTrayIcon::Information,3000); //最后一个参数为提示时长，默认10000，即10s

    QAction *windowAction = new QAction(QIcon(":/icons/star"), "主界面", this);
    connect(windowAction, SIGNAL(triggered()), this, SLOT(show()));
    QAction *liveDanmakuAction = new QAction(QIcon(":/icons/danmu"), "弹幕姬", this);
    connect(liveDanmakuAction, SIGNAL(triggered()), this, SLOT(on_actionShow_Live_Danmaku_triggered()));
    QAction *orderPlayerAction = new QAction(QIcon(":/icons/order_song"), "点歌姬", this);
    connect(orderPlayerAction, SIGNAL(triggered()), this, SLOT(on_actionShow_Order_Player_Window_triggered()));
    QAction *videoAction = new QAction(QIcon(":/icons/live"), "视频流", this);
    connect(videoAction, SIGNAL(triggered()), this, SLOT(on_actionShow_Live_Video_triggered()));
    QAction *quitAction = new QAction(QIcon(":/icons/cry"), "退出", this);
    connect(quitAction, SIGNAL(triggered()), this, SLOT(prepareQuit()));

    connect(tray,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),this,SLOT(trayAction(QSystemTrayIcon::ActivationReason)));



    bool melon = us->value("pk/autoMelon", false).toBool();
    ui->pkAutoMelonCheck->setChecked(melon);
    ui->pkAutoMaxGoldCheck->setChecked(us->value("pk/autoMaxGold", true).toBool());

    // 大乱斗自动赠送礼物
    ui->toutaGiftCheck->setChecked(us->value("danmaku/toutaGift").toBool());
    QString toutaGiftCountsStr = us->value("danmaku/toutaGiftCounts").toString();
    ui->toutaGiftCountsEdit->setText(toutaGiftCountsStr);
    liveRoomService->toutaGiftCounts.clear();
    foreach (QString s, toutaGiftCountsStr.split(" ", QString::SkipEmptyParts))
        liveRoomService->toutaGiftCounts.append(s.toInt());
    restoreToutaGifts(us->value("danmaku/toutaGifts", "").toString());


    // 自定义变量
    restoreCustomVariant(us->value("danmaku/customVariant", "").toString());
    restoreReplaceVariant(us->value("danmaku/replaceVariant", "").toString());

    // 多语言翻译
    restoreVariantTranslation();
    ui->autoWelcomeWordsEdit->updateCompleterModel();
    ui->autoThankWordsEdit->updateCompleterModel();
    ui->autoAttentionWordsEdit->updateCompleterModel();

    // 定时任务
    srand((unsigned)time(0));
    restoreTaskList();

    // 自动回复
    restoreReplyList();

    // 事件动作
    restoreEventList();

    // 保存舰长
    ui->saveEveryGuardCheck->setChecked(us->value("danmaku/saveEveryGuard", false).toBool());
    ui->saveMonthGuardCheck->setChecked(us->value("danmaku/saveMonthGuard", false).toBool());
    ui->saveEveryGiftCheck->setChecked(us->value("danmaku/saveEveryGift", false).toBool());

    // 自动发送
    ui->autoSendWelcomeCheck->setChecked(us->value("danmaku/sendWelcome", false).toBool());
    ui->autoSendGiftCheck->setChecked(us->value("danmaku/sendGift", false).toBool());
    ui->autoSendAttentionCheck->setChecked(us->value("danmaku/sendAttention", false).toBool());
    ui->sendWelcomeCDSpin->setValue(us->value("danmaku/sendWelcomeCD", 10).toInt());
    ui->sendGiftCDSpin->setValue(us->value("danmaku/sendGiftCD", 5).toInt());
    ui->sendAttentionCDSpin->setValue(us->value("danmaku/sendAttentionCD", 5).toInt());
    ui->autoWelcomeWordsEdit->setPlainText(us->value("danmaku/autoWelcomeWords", ui->autoWelcomeWordsEdit->toPlainText()).toString());
    ui->autoThankWordsEdit->setPlainText(us->value("danmaku/autoThankWords", ui->autoThankWordsEdit->toPlainText()).toString());
    ui->autoAttentionWordsEdit->setPlainText(us->value("danmaku/autoAttentionWords", ui->autoAttentionWordsEdit->toPlainText()).toString());
    ui->sendWelcomeTextCheck->setChecked(us->value("danmaku/sendWelcomeText", true).toBool());
    ui->sendWelcomeVoiceCheck->setChecked(us->value("danmaku/sendWelcomeVoice", false).toBool());
    ui->sendGiftTextCheck->setChecked(us->value("danmaku/sendGiftText", true).toBool());
    ui->sendGiftVoiceCheck->setChecked(us->value("danmaku/sendGiftVoice", false).toBool());
    ui->sendAttentionTextCheck->setChecked(us->value("danmaku/sendAttentionText", true).toBool());
    ui->sendAttentionVoiceCheck->setChecked(us->value("danmaku/sendAttentionVoice", false).toBool());
    ui->sendWelcomeTextCheck->setEnabled(ui->autoSendWelcomeCheck->isChecked());
    ui->sendWelcomeVoiceCheck->setEnabled(ui->autoSendWelcomeCheck->isChecked());
    ui->sendGiftTextCheck->setEnabled(ui->autoSendGiftCheck->isChecked());
    ui->sendGiftVoiceCheck->setEnabled(ui->autoSendGiftCheck->isChecked());
    ui->sendAttentionTextCheck->setEnabled(ui->autoSendAttentionCheck->isChecked());
    ui->sendAttentionVoiceCheck->setEnabled(ui->autoSendAttentionCheck->isChecked());

    // 文字转语音
    ui->autoSpeekDanmakuCheck->setChecked(us->value("danmaku/autoSpeek", false).toBool());
    ui->dontSpeakOnPlayingSongCheck->setChecked(us->value("danmaku/dontSpeakOnPlayingSong", false).toBool());
    if (ui->sendWelcomeVoiceCheck->isChecked() || ui->sendGiftVoiceCheck->isChecked()
            || ui->sendAttentionVoiceCheck->isChecked() || ui->autoSpeekDanmakuCheck->isChecked())
        initTTS();

    voicePlatform = static_cast<VoicePlatform>(us->value("voice/platform", 0).toInt());
    if (voicePlatform == VoiceLocal)
    {
        ui->voiceLocalRadio->setChecked(true);
        ui->voiceNameEdit->setText(us->value("voice/localName").toString());
    }
    else if (voicePlatform == VoiceXfy)
    {
        ui->voiceXfyRadio->setChecked(true);
        ui->voiceNameEdit->setText(us->value("xfytts/name").toString());
        ui->xfyAppIdEdit->setText(us->value("xfytts/appid").toString());
        ui->xfyApiKeyEdit->setText(us->value("xfytts/apikey").toString());
        ui->xfyApiSecretEdit->setText(us->value("xfytts/apisecret").toString());
    }
    else if (voicePlatform == VoiceMS)
    {
        ui->voiceConfigSettingsCard->hide();
        ui->voiceMSRadio->setChecked(true);
        ui->MSAreaCodeEdit->setText(us->value("mstts/areaCode").toString());
        ui->MSSubscriptionKeyEdit->setText(us->value("mstts/subscriptionKey").toString());
        msTTSFormat = us->value("mstts/format", DEFAULT_MS_TTS_SSML_FORMAT).toString();
    }
    else if (voicePlatform == VoiceCustom)
    {
        ui->voiceCustomRadio->setChecked(true);
        ui->voiceNameEdit->setText(us->value("voice/customName").toString());
    }

    ui->voicePitchSlider->setSliderPosition(us->value("voice/pitch", 50).toInt());
    ui->voiceSpeedSlider->setSliderPosition(us->value("voice/speed", 50).toInt());
    ui->voiceVolumeSlider->setSliderPosition(us->value("voice/volume", 50).toInt());
    ui->voiceCustomUrlEdit->setText(us->value("voice/customUrl", "").toString());

    // AI回复
    QString TXSecretId = us->value("tx_nlp/secretId").toString();
    if (!TXSecretId.isEmpty())
    {
        TxNlp::instance()->setSecretId(TXSecretId);
        ui->TXSecretIdEdit->setText(TXSecretId);
    }
    QString TXSecretKey = us->value("tx_nlp/secretKey").toString();
    if (!TXSecretKey.isEmpty())
    {
        TxNlp::instance()->setSecretKey(TXSecretKey);
        ui->TXSecretKeyEdit->setText(TXSecretKey);
    }
    connect(TxNlp::instance(), &TxNlp::signalError, this, [=](const QString& err){
        showError("智能闲聊", err);
    });

    // 开播
    ui->startLiveWordsEdit->setText(us->value("live/startWords").toString());
    ui->endLiveWordsEdit->setText(us->value("live/endWords").toString());
    ui->startLiveSendCheck->setChecked(us->value("live/startSend").toBool());

    // 启动动画
#ifdef ZUOQI_ENTRANCE
    ui->startupAnimationCheck->setChecked(us->value("mainwindow/splash", false).toBool());
#else
    ui->startupAnimationCheck->setChecked(us->value("mainwindow/splash", firstOpen).toBool());
#endif
    ui->enableTrayCheck->setChecked(us->value("mainwindow/enableTray", false).toBool());
    if (ui->enableTrayCheck->isChecked())
        tray->show(); // 让托盘图标显示在系统托盘上
    permissionText = us->value("mainwindow/permissionText", rt->asPlugin ? "Lite版" : permissionText).toString();

    // 定时连接
    ui->timerConnectServerCheck->setChecked(us->value("live/timerConnectServer", false).toBool());
    ui->startLiveHourSpin->setValue(us->value("live/startLiveHour", 0).toInt());
    ui->endLiveHourSpin->setValue(us->value("live/endLiveHour", 0).toInt());
    ui->timerConnectIntervalSpin->setValue(us->value("live/timerConnectInterval", 30).toInt());
    connectServerTimer = new QTimer(this);
    connectServerTimer->setInterval(ui->timerConnectIntervalSpin->value() * 60000);
    connect(connectServerTimer, &QTimer::timeout, this, [=]{
        connectServerTimer->setInterval(ui->timerConnectIntervalSpin->value() * 60000); // 比如服务器主动断开，则会短期内重新定时，还原自动连接定时
        if (isLiving() && (socket->state() == QAbstractSocket::ConnectedState || socket->state() == QAbstractSocket::ConnectingState))
        {
            connectServerTimer->stop();
            return ;
        }
        startConnectRoom();
    });

    // WS连接
    initWS();
    startConnectRoom();

    // 10秒内不进行自动化操作
    QTimer::singleShot(3000, [=]{
        justStart = false;
    });

    // 等待通道
    for (int i = 0; i < CHANNEL_COUNT; i++)
        msgWaits[i] = WAIT_CHANNEL_MAX;

    // 读取拼音1
    QtConcurrent::run([=]{
        QFile pinyinFile(":/documents/pinyin");
        pinyinFile.open(QIODevice::ReadOnly);
        QTextStream pinyinIn(&pinyinFile);
        pinyinIn.setCodec("UTF-8");
        QString line = pinyinIn.readLine();
        while (!line.isNull())
        {
            if (!line.isEmpty())
            {
                QString han = line.at(0);
                QString pinyin = line.right(line.length()-1);
                rt->pinyinMap.insert(han, pinyin);
            }
            line = pinyinIn.readLine();
        }
    });

    // 隐藏偷塔
    if (!us->value("danmaku/touta", false).toBool())
    {
        ui->pkAutoMelonCheck->setText("此项禁止使用");
        ui->danmakuToutaSettingsCard->hide();
        ui->scrollArea->removeWidget(ui->danmakuToutaSettingsCard);
        ui->toutaGiftSettingsCard->hide();
        ui->scrollArea->removeWidget(ui->toutaGiftSettingsCard);
    }

    // 粉丝勋章
    ui->autoSwitchMedalCheck->setChecked(us->value("danmaku/autoSwitchMedal", false).toBool());

    // 读取自定义快捷房间
    QStringList list = us->value("custom/rooms", "").toString().split(";", QString::SkipEmptyParts);
    ui->menu_3->addSeparator();
    for (int i = 0; i < list.size(); i++)
    {
        QStringList texts = list.at(i).split(",", QString::SkipEmptyParts);
        if (texts.size() < 1)
            continue ;
        QString id = texts.first();
        QString name = texts.size() >= 2 ? texts.at(1) : id;
        QAction* action = new QAction(name, this);
        ui->menu_3->addAction(action);
        connect(action, &QAction::triggered, this, [=]{
            ui->roomIdEdit->setText(id);
            on_roomIdEdit_editingFinished();
        });
    }

    // 滚屏
    ui->enableScreenDanmakuCheck->setChecked(us->value("screendanmaku/enableDanmaku", false).toBool());
    ui->enableScreenMsgCheck->setChecked(us->value("screendanmaku/enableMsg", false).toBool());
    ui->screenDanmakuWithNameCheck->setChecked(us->value("screendanmaku/showName", true).toBool());
    ui->screenDanmakuLeftSpin->setValue(us->value("screendanmaku/left", 0).toInt());
    ui->screenDanmakuRightSpin->setValue(us->value("screendanmaku/right", 0).toInt());
    ui->screenDanmakuTopSpin->setValue(us->value("screendanmaku/top", 10).toInt());
    ui->screenDanmakuBottomSpin->setValue(us->value("screendanmaku/bottom", 60).toInt());
    ui->screenDanmakuSpeedSpin->setValue(us->value("screendanmaku/speed", 10).toInt());
    ui->enableScreenMsgCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
    ui->screenDanmakuWithNameCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
    QString danmakuFontString = us->value("screendanmaku/font").toString();
    if (!danmakuFontString.isEmpty())
        screenDanmakuFont.fromString(danmakuFontString);
    screenDanmakuColor = qvariant_cast<QColor>(us->value("screendanmaku/color", QColor(0, 0, 0)));
    connect(this, &MainWindow::signalNewDanmaku, this, [=](const LiveDanmaku &danmaku){
//        QtConcurrent::run([&]{
            showScreenDanmaku(danmaku);
//        });
    });

    connect(this, &MainWindow::signalNewDanmaku, this, [=](const LiveDanmaku &danmaku){
        if (danmaku.isPkLink()) // 大乱斗对面的弹幕不朗读
            return ;
        if (!_loadingOldDanmakus && ui->autoSpeekDanmakuCheck->isChecked() && danmaku.getMsgType() == MSG_DANMAKU
                && shallSpeakText())
        {
            if (hasSimilarOldDanmaku(danmaku.getText()))
                return ;
            speakText(danmaku.getText());
        }
    });

    // 自动签到
    if (us->value("danmaku/autoDoSign", false).toBool())
    {
        ui->autoDoSignCheck->setChecked(true);
    }

    // 自动参与天选
    ui->autoLOTCheck->setChecked(us->value("danmaku/autoLOT", false).toBool());

    // 自动获取小心心
    ui->acquireHeartCheck->setChecked(us->value("danmaku/acquireHeart", false).toBool());
    ui->heartTimeSpin->setValue(us->value("danmaku/acquireHeartTime", 120).toInt());
    todayHeartMinite = us->value("danmaku/todayHeartMinite").toInt();
    ui->acquireHeartCheck->setToolTip("今日已领" + snum(todayHeartMinite/5) + "个小心心(" + snum(todayHeartMinite) + "分钟)");

    // 自动赠送过期礼物
    ui->sendExpireGiftCheck->setChecked(us->value("danmaku/sendExpireGift", false).toBool());

    // 永久禁言
    QJsonArray eternalBlockArray = us->value("danmaku/eternalBlockUsers").toJsonArray();
    int eternalBlockSize = eternalBlockArray.size();
    for (int i = 0; i < eternalBlockSize; i++)
    {
        EternalBlockUser eb = EternalBlockUser::fromJson(eternalBlockArray.at(i).toObject());
        if (eb.uid && eb.roomId)
            us->eternalBlockUsers.append(eb);
    }

#ifndef ZUOQI_ENTRANCE
    // 开机自启
    ui->startOnRebootCheck->setChecked(us->value("runtime/startOnReboot", false).toBool());
    // 自动更新
    ui->autoUpdateCheck->setChecked(us->value("runtime/autoUpdate", !rt->asPlugin).toBool());
    ui->autoUpdateCheck->setEnabled(!rt->asPlugin);
    ui->showChangelogCheck->setChecked(us->value("runtime/showChangelog", true).toBool());
    ui->updateBetaCheck->setChecked(us->value("runtime/updateBeta", false).toBool());
#endif

    // 每分钟定时
    minuteTimer = new QTimer(this);
    minuteTimer->setInterval(60000);
    connect(minuteTimer, &QTimer::timeout, this, [=]{
        // 直播间人气
        if (ac->currentPopul > 1 && isLiving()) // 为0的时候不计入内；为1时可能机器人在线
        {
            sumPopul += ac->currentPopul;
            countPopul++;

            dailyAvePopul = int(sumPopul / countPopul);
            if (dailySettings)
                dailySettings->setValue("average_popularity", dailyAvePopul);
        }
        if (dailyMaxPopul < ac->currentPopul)
        {
            dailyMaxPopul = ac->currentPopul;
            if (dailySettings)
                dailySettings->setValue("max_popularity", dailyMaxPopul);
        }

        // 弹幕人气
        danmuPopulValue += minuteDanmuPopul;
        danmuPopulQueue.append(minuteDanmuPopul);
        minuteDanmuPopul = 0;
        if (danmuPopulQueue.size() > 5)
            danmuPopulValue -= danmuPopulQueue.takeFirst();
        ui->danmuCountLabel->setToolTip("5分钟弹幕人气：" + snum(danmuPopulValue) + "，平均人气：" + snum(dailyAvePopul));

        triggerCmdEvent("DANMU_POPULARITY", LiveDanmaku(), false);
    });

    // 每小时整点的的事件
    hourTimer = new QTimer(this);
    QTime currentTime = QTime::currentTime();
    QTime nextTime = currentTime;
    nextTime.setHMS((currentTime.hour() + 1) % 24, 0, 1);
    hourTimer->setInterval(currentTime.hour() < 23 ? currentTime.msecsTo(nextTime) : 3600000);
    connect(hourTimer, &QTimer::timeout, this, [=]{
        QTime currentTime = QTime::currentTime();
        QTime nextTime = currentTime;
        nextTime.setHMS((currentTime.hour() + 1) % 24, 0, 1);
        hourTimer->setInterval(currentTime.hour() < 23 ? currentTime.msecsTo(nextTime) : 3600000);

        // 永久禁言
        detectEternalBlockUsers();

        // 自动签到
        if (ui->autoDoSignCheck->isChecked())
        {
            int hour = QTime::currentTime().hour();
            if (hour == 0)
            {
                doSign();
            }
        }

        // 版权声明
        if (isLiving() && !us->value("danmaku/copyright", false).toBool()
                && qrand() % 3 == 0)
        {
            /* if (shallAutoMsg() && (ui->autoSendWelcomeCheck->isChecked() || ui->autoSendGiftCheck->isChecked() || ui->autoSendAttentionCheck->isChecked()))
            {
                sendMsg(QString(QByteArray::fromBase64("562U6LCi5aes44CQ"))
                        +QApplication::applicationName()
                        +QByteArray::fromBase64("44CR5Li65oKo5pyN5Yqhfg=="));
            } */
        }
        triggerCmdEvent("NEW_HOUR", LiveDanmaku(), false);
        qInfo() << "NEW_HOUR:" << QDateTime::currentDateTime();

        // 判断每天最后一小时
        // 以 23:59:30为准
        QDateTime current = QDateTime::currentDateTime();
        QTime t = current.time();
        if (todayIsEnding) // 已经触发最后一小时事件了
        {
        }
        else if (current.time().hour() == 23
                || (t.hour() == 22 && t.minute() == 59 && t.second() > 30)) // 22:59:30之后的
        {
            todayIsEnding = true;
            QDateTime dt = current;
            QTime t = dt.time();
            t.setHMS(23, 59, 30); // 移动到最后半分钟
            dt.setTime(t);
            qint64 delta = dt.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
            if (delta < 0) // 可能已经是即将最后了
                delta = 0;
            QTimer::singleShot(delta, [=]{
                triggerCmdEvent("DAY_END", LiveDanmaku(), true);

                // 判断每月最后一天
                QDate d = current.date();
                int days = d.daysInMonth();
                if (d.day() == days) // 1~31 == 28~31
                {
                    triggerCmdEvent("MONTH_END", LiveDanmaku(), true);

                    // 判断每年最后一天
                    days = d.daysInYear();
                    if (d.dayOfYear() == days)
                    {
                        triggerCmdEvent("YEAR_END", LiveDanmaku(), true);
                    }
                }

                // 判断每周最后一天
                if (d.dayOfWeek() == 7)
                {
                    triggerCmdEvent("WEEK_END", LiveDanmaku(), true);
                }
            });
        }
    });
    hourTimer->start();

    // 每天的事件
    dayTimer = new QTimer(this);
    QTime zeroTime = QTime::currentTime();
    zeroTime.setHMS(0, 0, 1); // 本应当完全0点整的，避免误差
    QDate tomorrowDate = QDate::currentDate();
    tomorrowDate = tomorrowDate.addDays(1);
    QDateTime tomorrow(tomorrowDate, zeroTime);
    dayTimer->setInterval(QDateTime::currentDateTime().msecsTo(tomorrow));
    // 判断新的一天
    connect(dayTimer, &QTimer::timeout, this, [=]{
        todayIsEnding = false;
        qInfo() << "--------NEW_DAY" << QDateTime::currentDateTime();
        {
            // 当前时间必定是 0:0:1，误差0.2秒内
            QDate tomorrowDate = QDate::currentDate();
            tomorrowDate = tomorrowDate.addDays(1);
            QDateTime tomorrow(tomorrowDate, zeroTime);
            dayTimer->setInterval(QDateTime::currentDateTime().msecsTo(tomorrow));
        }

        // 每天重新计算
        if (ui->calculateDailyDataCheck->isChecked())
            startCalculateDailyData();
        if (danmuLogFile /* && !isLiving() */)
            startSaveDanmakuToFile();
        us->userComeTimes.clear();
        sumPopul = 0;
        countPopul = 0;

        // 触发每天事件
        const QDate currDate = QDate::currentDate();
        triggerCmdEvent("NEW_DAY", LiveDanmaku(), true);
        triggerCmdEvent("NEW_DAY_FIRST", LiveDanmaku(), true);
        us->setValue("runtime/open_day", currDate.day());
        updatePermission();

        processNewDay();
        qInfo() << "当前 month =" << currDate.month() << ", day =" << currDate.day() << ", week =" << currDate.dayOfWeek();

        // 判断每一月初
        if (currDate.day() == 1)
        {
            triggerCmdEvent("NEW_MONTH", LiveDanmaku(), true);
            triggerCmdEvent("NEW_MONTH_FIRST", LiveDanmaku(), true);
            us->setValue("runtime/open_month", currDate.month());

            // 判断每一年初
            if (currDate.month() == 1)
            {
                triggerCmdEvent("NEW_YEAR", LiveDanmaku(), true);
                triggerCmdEvent("NEW_YEAR_FIRST", LiveDanmaku(), true);
                triggerCmdEvent("HAPPY_NEW_YEAR", LiveDanmaku(), true);
                us->setValue("runtime/open_year", currDate.year());
            }
        }

        // 判断每周一
        if (currDate.dayOfWeek() == 1)
        {
            triggerCmdEvent("NEW_WEEK", LiveDanmaku(), true);
            triggerCmdEvent("NEW_WEEK_FIRST", LiveDanmaku(), true);
            us->setValue("runtime/open_week_number", currDate.weekNumber());
        }
    });
    dayTimer->start();

    // 判断第一次打开
    QDate currDate = QDate::currentDate();
    int prevYear = us->value("runtime/open_year", -1).toInt();
    int prevMonth = us->value("runtime/open_month", -1).toInt();
    int prevDay = us->value("runtime/open_day", -1).toInt();
    int prevWeekNumber = us->value("runtime/open_week_number", -1).toInt();
    if (prevYear != currDate.year())
    {
        prevMonth = prevDay = -1; // 避免是不同年的同一月
        triggerCmdEvent("NEW_YEAR_FIRST", LiveDanmaku(), true);
        us->setValue("runtime/open_year", currDate.year());
    }
    if (prevMonth != currDate.month())
    {
        prevDay = -1; // 避免不同月的同一天
        triggerCmdEvent("NEW_MONTH_FIRST", LiveDanmaku(), true);
        us->setValue("runtime/open_month", currDate.month());
    }
    if (prevDay != currDate.day())
    {
        triggerCmdEvent("NEW_DAY_FIRST", LiveDanmaku(), true);
        us->setValue("runtime/open_day", currDate.day());
    }
    if (prevWeekNumber != currDate.weekNumber())
    {
        triggerCmdEvent("NEW_WEEK_FIRST", LiveDanmaku(), true);
        us->setValue("runtime/open_week_number", currDate.weekNumber());
    }

    // sync
    syncTimer = new QTimer(this);
    syncTimer->setSingleShot(true);
    connect(syncTimer, &QTimer::timeout, this, [=]{
        /* if (ac->roomId.isEmpty() || !isLiving()) // 使用一段时间后才算真正用上
            return ; */
        syncMagicalRooms();
    });

    // permission
    permissionTimer = new QTimer(this);
    connect(permissionTimer, &QTimer::timeout, this, [=]{
        updatePermission();
    });

    // 互动玩法
    ui->identityCodeEdit->setText(ac->identityCode = us->value("live-open/identityCode").toString());
    ui->liveOpenCheck->setChecked(us->value("live-open/enabled").toBool());

    // 数据清理
    ui->autoClearComeIntervalSpin->setValue(us->value("danmaku/clearDidntComeInterval", 7).toInt());

    // 调试模式
    localDebug = us->value("debug/localDebug", false).toBool();
    ui->actionLocal_Mode->setChecked(localDebug);
    debugPrint = us->value("debug/debugPrint", false).toBool();
    ui->actionDebug_Mode->setChecked(debugPrint);
    saveRecvCmds = us->value("debug/saveRecvCmds", false).toBool();
    ui->saveRecvCmdsCheck->setChecked(saveRecvCmds);
    if (saveRecvCmds)
        on_saveRecvCmdsCheck_clicked();
    ui->saveLogCheck->setChecked(us->value("debug/logFile", false).toBool());

    // 模拟CMDS
    ui->timerPushCmdCheck->setChecked(us->value("debug/pushCmdsTimer", false).toBool());
    ui->timerPushCmdSpin->setValue(us->value("debug/pushCmdsInterval", 10).toInt());

    if (!us->value("danmaku/copyright", false).toBool())
    {
        /* if (shallAutoMsg() && (ui->autoSendWelcomeCheck->isChecked() || ui->autoSendGiftCheck->isChecked() || ui->autoSendAttentionCheck->isChecked()))
        {
            localNotify(QString(QByteArray::fromBase64("44CQ"))
                        +QApplication::applicationName()
                        +QByteArray::fromBase64("44CR5Li65oKo5pyN5Yqhfg=="));
        } */
    }

    // 开启服务端
    bool enableServer = us->value("server/enabled", false).toBool();
    ui->serverCheck->setChecked(enableServer);
    int port = us->value("server/port", 5520).toInt();
    ui->serverPortSpin->setValue(port);
    serverDomain = us->value("server/domain", "localhost").toString();
    ui->allowWebControlCheck->setChecked(us->value("server/allowWebControl", false).toBool());
    ui->allowRemoteControlCheck->setChecked(remoteControl = us->value("danmaku/remoteControl", true).toBool());
    ui->allowAdminControlCheck->setChecked(us->value("danmaku/adminControl", false).toBool());
    ui->domainEdit->setText(serverDomain);
    if (enableServer)
    {
        openServer();
    }

    // 加载网页
    loadWebExtensionList();

    // 设置默认配置
    if (firstOpen)
    {
        readDefaultCode();
        us->setValue("runtime/first_use_time", QDateTime::currentSecsSinceEpoch());
    }

    // 恢复游戏数据
    restoreGameNumbers();
    restoreGameTexts();

    // Lite版
    if (rt->asPlugin)
    {
        ui->droplight->setText("Lite版");
        ui->label_52->setText("<html><head/><body><p>暂</p><p>且</p><p>留</p><p>空</p></body></html>");
    }
    if (rt->asFreeOnly)
    {
        ui->vipExtensionButton->setText("Lite版未安装回复、事件等功能");
        ui->existExtensionsLabel->setText("Lite版不支持插件系统");
    }

    // 回复统计数据
    int appOpenCount = us->value("mainwindow/appOpenCount", 0).toInt();
    us->setValue("mainwindow/appOpenCount", ++appOpenCount);
    ui->robotSendCountTextLabel->setToolTip("累计启动 " + snum(appOpenCount) + " 次");
}

/**
 * 第二次读取设置
 * 登录后再进行
 */
void MainWindow::readConfig2()
{
    // 数据库
    saveToSqlite = hasPermission() && us->value("db/sqlite").toBool();
    ui->saveToSqliteCheck->setChecked(saveToSqlite);
    ui->saveCmdToSqliteCheck->setChecked(saveCmdToSqlite = us->value("db/cmd").toBool());
    ui->saveCmdToSqliteCheck->setEnabled(saveToSqlite);
    if (saveToSqlite)
        sqlService.open();
}

void MainWindow::initEvent()
{
    initLiveRecord();
}

void MainWindow::adjustPageSize(int page)
{
    if (page == PAGE_ROOM)
    {
        // 自动调整封面大小
        if (!roomCover.isNull())
        {
            adjustCoverSizeByRoomCover(roomCover);

            /* int w = ui->roomCoverLabel->width();
            if (w > ui->tabWidget->contentsRect().width())
                w = ui->tabWidget->contentsRect().width();
            pixmap = pixmap.scaledToWidth(w, Qt::SmoothTransformation);
            ui->roomCoverLabel->setPixmap(getRoundedPixmap(pixmap));
            ui->roomCoverLabel->setMinimumSize(1, 1); */
        }
    }
    else if (page == PAGE_DANMAKU)
    {

    }
    else if (page == PAGE_THANK)
    {
        customVarsButton->move(ui->thankStackedWidget->geometry().right() - customVarsButton->width(),
                               ui->thankTopTabGroup->y());
    }
    else if (page == PAGE_MUSIC)
    {
        {
            QRect g = ui->musicBigTitleLabel->geometry();
            musicTitleDecorateWidget->move(g.right() - g.width()/4, g.top() - musicTitleDecorateWidget->height()/2 + g.height());
        }
    }
    else if (page == PAGE_EXTENSION)
    {
        extensionButton->move(ui->tabWidget->width() - extensionButton->height(), 0);

        appendListItemButton->move(ui->tabWidget->width() - appendListItemButton->width() * 1.25 - 9 - 15, // 减去滚动条宽度
                                   ui->tabWidget->height() - appendListItemButton->height() * 1.25 - 9); // 还有减去margin

        // 自动调整列表大小
        int tabIndex = ui->tabWidget->currentIndex();
        if (tabIndex == TAB_TIMER_TASK)
        {
            static bool first = false;
            if (!first)
            {
                first = true;
                QTimer::singleShot(0, [=]{
                    ui->taskListWidget->verticalScrollBar()->setSliderPosition(us->i("mainwindow/timerTaskScroll"));
                });
            }
            for (int row = 0; row < ui->taskListWidget->count(); row++)
            {
                auto item = ui->taskListWidget->item(row);
                auto widget = ui->taskListWidget->itemWidget(item);
                if (!widget)
                    continue;
                QSize size(ui->taskListWidget->contentsRect().width() - ui->taskListWidget->verticalScrollBar()->width(), widget->height());
                auto taskWidget = static_cast<TaskWidget*>(widget);
                taskWidget->resize(size);
                taskWidget->autoResizeEdit();
            }
        }
        else if (tabIndex == TAB_AUTO_REPLY)
        {
            static bool first = false;
            if (!first)
            {
                first = true;
                QTimer::singleShot(0, [=]{
                    ui->replyListWidget->verticalScrollBar()->setSliderPosition(us->i("mainwindow/autoReplyScroll"));
                });
            }
            for (int row = 0; row < ui->replyListWidget->count(); row++)
            {
                auto item = ui->replyListWidget->item(row);
                auto widget = ui->replyListWidget->itemWidget(item);
                if (!widget)
                    continue;
                QSize size(ui->replyListWidget->contentsRect().width() - ui->replyListWidget->verticalScrollBar()->width(), widget->height());
                auto replyWidget = static_cast<ReplyWidget*>(widget);
                replyWidget->resize(size);
                replyWidget->autoResizeEdit();
            }
        }
        else if (tabIndex == TAB_EVENT_ACTION)
        {
            static bool first = false;
            if (!first)
            {
                first = true;
                QTimer::singleShot(0, [=]{
                    ui->eventListWidget->verticalScrollBar()->setSliderPosition(us->i("mainwindow/eventActionScroll"));
                });
            }
            for (int row = 0; row < ui->eventListWidget->count(); row++)
            {
                auto item = ui->eventListWidget->item(row);
                auto widget = ui->eventListWidget->itemWidget(item);
                if (!widget)
                    continue;
                QSize size(ui->eventListWidget->contentsRect().width() - ui->eventListWidget->verticalScrollBar()->width(), widget->height());
                auto eventWidget = static_cast<EventWidget*>(widget);
                eventWidget->resize(size);
                eventWidget->autoResizeEdit();
            }
        }
    }
    else if (page == PAGE_PREFENCE)
    {

    }
}

/// 显示动画
/// 在不影响布局的基础上
void MainWindow::switchPageAnimation(int page)
{
    // 房间ID，可能经常触发
    if (page == PAGE_ROOM)
    {
        adjustRoomIdWidgetPos();
        showRoomIdWidget();
        if (!roomCover.isNull())
            adjustCoverSizeByRoomCover(roomCover);
    }
    else
    {
        hideRoomIdWidget();
    }

    auto startGeometryAni = [=](QWidget* widget, QVariant start, QVariant end, int duration = 400, QEasingCurve curve = QEasingCurve::OutCubic){
        if (widget->isHidden())
            widget->show();
        QPropertyAnimation* ani = new QPropertyAnimation(widget, "geometry");
        ani->setStartValue(start);
        ani->setEndValue(end);
        ani->setEasingCurve(curve);
        ani->setDuration(duration);
        connect(ani, SIGNAL(finished()), ani, SLOT(deleteLater()));
        ani->start();
    };

    auto startGeometryAniDelay = [&](int delay, QWidget* widget, QVariant start, QVariant end, int duration = 400, QEasingCurve curve = QEasingCurve::OutCubic){
        widget->resize(0, 0);
        QTimer::singleShot(delay, [=]{
            startGeometryAni(widget, start, end, duration, curve);
        });
    };

    if (page == PAGE_ROOM)
    {
        if (ui->upHeaderLabel->pixmap())
        {
            QLabel* label = new QLabel(this);
            label->setFixedSize(ui->upHeaderLabel->size());
            QPixmap pixmap = *ui->upHeaderLabel->pixmap();
            label->setPixmap(pixmap);
            label->show();
            QPoint pos(ui->upHeaderLabel->mapTo(this, QPoint(0, 0)));

            QPropertyAnimation* ani = new QPropertyAnimation(label, "pos");
            ani->setStartValue(QPoint(pos.x(), this->height()));
            ani->setEndValue(pos);
            ani->setEasingCurve(QEasingCurve::OutBack);
            ani->setDuration(400);
            connect(ani, &QPropertyAnimation::finished, this, [=]{
                ani->deleteLater();
                label->deleteLater();
                ui->upHeaderLabel->setPixmap(toCirclePixmap(pixmap));
            });
            ani->start();

            QPixmap emptyPixmap(pixmap.size());
            emptyPixmap.fill(Qt::transparent);
            ui->upHeaderLabel->setStyleSheet("");
            ui->upHeaderLabel->setPixmap(emptyPixmap);
        }

    }
    else if (page == PAGE_DANMAKU)
    {
        static bool restored = false;
        if (!restored)
        {
            restored = true;
            QTimer::singleShot(0, [=]{
                ui->scrollArea->verticalScrollBar()->setSliderPosition(us->i("mainwindow/configFlowScroll"));
            });
        }

        QRect g(ui->danmakuTitleSplitWidget1->geometry());
        startGeometryAni(ui->danmakuTitleSplitWidget1,
                         QRect(g.right()-1, g.top(), 1, 1), g);

        g = QRect(ui->danmakuTitleSplitWidget2->geometry());
        startGeometryAni(ui->danmakuTitleSplitWidget2,
                         QRect(g.left(), g.top(), 1, 1), g);
    }
    else if (page == PAGE_THANK)
    {
        QRect g(ui->thankTopTabGroup->geometry());
        startGeometryAni(ui->thankTopTabGroup,
                         QRect(g.center().x(), g.top(), 1, g.height()), g);
    }
    else if (page == PAGE_MUSIC)
    {
//        ui->musicContainerWidget->layout()->setEnabled(false);
//        QTimer::singleShot(900, [=]{
//            ui->musicContainerWidget->layout()->setEnabled(true);
//        });

        // 标题装饰文字背景动画
        QRect g(musicTitleDecorateWidget->geometry());
        startGeometryAni(musicTitleDecorateWidget,
                         QRect(g.left(), -g.height(), g.width(), g.height()), g);

        // 下划线展开动画
        g = QRect(ui->musicSplitWidget1->geometry());
        startGeometryAniDelay(500, ui->musicSplitWidget1,
                         QRect(g.center().x(), g.top(), 1, 1), g);

        // 没开启的话总开关动画高亮
        if (!ui->DiangeAutoCopyCheck->isChecked())
        {
            g = ui->DiangeAutoCopyCheck->geometry();
            startGeometryAniDelay(200, ui->DiangeAutoCopyCheck,
                             QRect(g.left() - g.width(), g.top(), g.width(), g.height()), g,
                                  600, QEasingCurve::OutBounce);
        }

        // 三个小功能开关的设置项
//        ui->label_14->hide();
//        ui->label_43->hide();
//        ui->label_44->hide();
//        ui->label_14->setMinimumSize(0, 0);
//        ui->label_43->setMinimumSize(0, 0);
//        ui->label_44->setMinimumSize(0, 0);
//        g = ui->label_41->geometry();
//        startGeometryAni(ui->label_41,
//                         QRect(g.center(), QPoint(1, 1)), g);
//        g = ui->label_43->geometry();
//        startGeometryAniDelay(300, ui->label_43,
//                         QRect(g.center(), QPoint(1, 1)), g);
//        g = ui->label_44->geometry();
//        startGeometryAniDelay(600, ui->label_44,
//                         QRect(g.center(), QPoint(1, 1)), g);

    }
    else if (page == PAGE_EXTENSION)
    {
        QRect g(appendListItemButton->geometry());
        if (!appendListItemButton->isHidden())
        {
            startGeometryAni(appendListItemButton,
                             QRect(g.left(), ui->tabWidget->height(), g.width(), g.height()), g,
                             400, QEasingCurve::OutBack);
        }
    }
    else if (page == PAGE_PREFENCE)
    {
        QRect g(ui->label_36->geometry());
        startGeometryAni(ui->label_36,
                         QRect(g.center(), QSize(1,1)), g,
                         400, QEasingCurve::OutCubic);

        g = QRect(ui->label_39->geometry());
        startGeometryAniDelay(400, ui->label_39,
                         QRect(g.center(), QSize(1,1)), g,
                         400, QEasingCurve::OutCubic);
    }
}

MainWindow::~MainWindow()
{
    // 保存配置
    if (danmuLogFile)
    {
        finishSaveDanmuToFile();
    }
    if (ui->calculateDailyDataCheck->isChecked())
    {
        saveCalculateDailyData();
    }
    us->setValue("mainwindow/configFlowScroll", ui->scrollArea->verticalScrollBar()->sliderPosition());
    us->setValue("mainwindow/timerTaskScroll", ui->taskListWidget->verticalScrollBar()->sliderPosition());
    us->setValue("mainwindow/autoReplyScroll", ui->replyListWidget->verticalScrollBar()->sliderPosition());
    us->setValue("mainwindow/eventActionScroll", ui->eventListWidget->verticalScrollBar()->sliderPosition());
    us->sync();

    // 关闭窗口
    if (danmakuWindow)
    {
        danmakuWindow->close();
        danmakuWindow->deleteLater();
        danmakuWindow = nullptr;
    }

    if (musicWindow)
    {
        delete musicWindow;
        musicWindow = nullptr;
    }

    /*if (playerWindow)
    {
        settings->setValue("danmaku/playerWindow", !playerWindow->isHidden());
        playerWindow->close();
        playerWindow->deleteLater();
    }*/

    triggerCmdEvent("SHUT_DOWN", LiveDanmaku(), true);

    // 清理过期备份
    auto files = QDir(rt->dataPath + "backup").entryInfoList(QDir::NoDotAndDotDot | QDir::Files);
    qint64 overdue = QDateTime::currentSecsSinceEpoch() - 3600 * 24 * qMax(ui->autoClearComeIntervalSpin->value(), 7); // 至少备份7天
    foreach (auto info, files)
    {
        if (info.lastModified().toSecsSinceEpoch() < overdue)
        {
            qInfo() << "删除备份：" << info.absoluteFilePath();
            deleteFile(info.absoluteFilePath());
        }
    }

    // 删除缓存
    if (isFileExist(webCache("")))
        deleteDir(webCache(""));

    // 删除界面
    delete ui;
    tray->deleteLater();

    // 自动更新
    QString pkgPath = QApplication::applicationDirPath() + "/update.zip";
    if (isFileExist(pkgPath) && QFileInfo(pkgPath).size() > 100 * 1024) // 起码大于100K，可能没更新完
    {
        // 更新历史版本
        qInfo() << "检测到已下载的安装包，进行更新";
        QString appPath = QApplication::applicationDirPath();
        QProcess process;
        process.startDetached(UPDATE_TOOL_NAME, { "-u", pkgPath, appPath, "-d", "-4"} );
    }
}

const QSettings* MainWindow::getSettings() const
{
    return us;
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    static bool firstShow = true;
    if (firstShow)
    {
        firstShow = false;

        // 恢复窗口位置
        restoreGeometry(us->value("mainwindow/geometry").toByteArray());

        // 显示启动动画
        startSplash();

        // 显示 RoomIdEdit 动画
        adjustRoomIdWidgetPos();
        if (ui->stackedWidget->currentIndex() != 0)
            roomIdBgWidget->hide();
    }
    us->setValue("mainwindow/autoShow", true);

    if (!roomCover.isNull())
        adjustCoverSizeByRoomCover(roomCover);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    us->setValue("mainwindow/geometry", this->saveGeometry());
    us->sync();

#if defined(ENABLE_TRAY)
    if (!tray->isVisible())
    {
        prepareQuit();
        return ;
    }

    event->ignore();
    this->hide();
    QTimer::singleShot(5000, [=]{
        if (!this->isHidden())
            return ;
        us->setValue("mainwindow/autoShow", false);
    });
#else
    QMainWindow::closeEvent(event);
#endif
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    adjustPageSize(ui->stackedWidget->currentIndex());
    tip_box->adjustPosition();

    if (fakeEntrance)
    {
        fakeEntrance->setGeometry(this->rect());
    }
}

void MainWindow::changeEvent(QEvent *event)
{

    QMainWindow::changeEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    QLinearGradient linearGrad(QPointF(width() / 2, 0), QPointF(width() / 2, height()));
    linearGrad.setColorAt(0, themeBg);
    double pos = 1.0;
    if (dailyMaxPopul > 100 && dailyAvePopul > 100)
    {
        pos = dailyAvePopul / double(dailyMaxPopul);
        pos = qMin(qMax(pos, 0.3), 1.0);
    }
    linearGrad.setColorAt(pos, themeGradient);
//    linearGrad.setColorAt(0, themeGradient);
//    linearGrad.setColorAt(1, Qt::white);
    painter.fillRect(rect(), linearGrad);
}

/// 恢复之前的弹幕
void MainWindow::pullLiveDanmaku()
{
    if (ac->roomId.isEmpty())
        return ;
    QString url = "https://api.live.bilibili.com/ajax/msg";
    QStringList param{"roomid", ac->roomId};
    connect(new NetUtil(url, param), &NetUtil::finished, this, [=](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "pullLiveDanmaku.ERROR:" << error.errorString();
            qCritical() << result;
            return ;
        }

        _loadingOldDanmakus = true;
        QJsonObject json = document.object();
        QJsonArray danmakus = json.value("data").toObject().value("room").toArray();
        QDateTime time = QDateTime::currentDateTime();
        qint64 removeTime = time.toMSecsSinceEpoch() - removeDanmakuInterval;
        for (int i = 0; i < danmakus.size(); i++)
        {
            LiveDanmaku danmaku = LiveDanmaku::fromDanmakuJson(danmakus.at(i).toObject());
            if (danmaku.getTimeline().toMSecsSinceEpoch() < removeTime)
                continue;
            danmaku.transToDanmu();
            danmaku.setTime(time);
            danmaku.setNoReply();
            appendNewLiveDanmaku(danmaku);
        }
        _loadingOldDanmakus = false;
    });
}

void MainWindow::removeTimeoutDanmaku()
{
    if (pushCmdsFile && roomDanmakus.size() < 1000) // 不移除弹幕
        return ;

    // 移除过期队列
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    qint64 removeTime = timestamp - removeDanmakuInterval;
    if (roomDanmakus.size())
    {
        QDateTime dateTime = roomDanmakus.first().getTimeline();
        if (dateTime.toMSecsSinceEpoch() < removeTime)
        {
            auto danmaku = roomDanmakus.takeFirst();
            oldLiveDanmakuRemoved(danmaku);
        }
    }

    // 移除多余的提示（一般时间更短）
    removeTime = timestamp - removeDanmakuTipInterval;
    for (int i = 0; i < roomDanmakus.size(); i++)
    {
        auto danmaku = roomDanmakus.at(i);
        auto type = danmaku.getMsgType();
        if (type == MSG_ATTENTION || type == MSG_WELCOME || type == MSG_FANS
                || (type == MSG_GIFT && (!danmaku.isGoldCoin() || danmaku.getTotalCoin() < 1000))
                || (type == MSG_DANMAKU && danmaku.isNoReply())
                || type == MSG_MSG
                || danmaku.isToView() || danmaku.isPkLink())
        {
            QDateTime dateTime = danmaku.getTimeline();
            if (dateTime.toMSecsSinceEpoch() < removeTime)
            {
                roomDanmakus.removeAt(i--);
                oldLiveDanmakuRemoved(danmaku);
            }
            // break; // 不break，就是一次性删除多个
        }
    }
}

void MainWindow::appendNewLiveDanmakus(const QList<LiveDanmaku> &danmakus)
{
    // 添加到队列
    roomDanmakus.append(danmakus);
    rt->allDanmakus.append(danmakus);
}

void MainWindow::appendNewLiveDanmaku(const LiveDanmaku &danmaku)
{
    roomDanmakus.append(danmaku);
    lastDanmaku = danmaku;
    rt->allDanmakus.append(danmaku);
    newLiveDanmakuAdded(danmaku);
}

void MainWindow::newLiveDanmakuAdded(const LiveDanmaku &danmaku)
{
    SOCKET_DEB << "+++++新弹幕：" << danmaku.toString();
    emit signalNewDanmaku(danmaku);

    // 保存到文件
    if (danmuLogStream)
    {
        if (danmaku.is(MSG_DEF) && danmaku.getText().startsWith("["))
            return ;
        (*danmuLogStream) << danmaku.toString() << "\n";
        (*danmuLogStream).flush(); // 立刻刷新到文件里
    }
}

void MainWindow::oldLiveDanmakuRemoved(const LiveDanmaku &danmaku)
{
    SOCKET_DEB << "-----旧弹幕：" << danmaku.toString();
    emit signalRemoveDanmaku(danmaku);
}

void MainWindow::addNoReplyDanmakuText(const QString &text)
{
    noReplyMsgs.append(text);
}

bool MainWindow::hasSimilarOldDanmaku(const QString& s) const
{
    int count = us->danmuSimilarJudgeCount;
    for (int i = rt->allDanmakus.size() - 2; i >= 0; i--)
    {
        const LiveDanmaku& danmaku = rt->allDanmakus.at(i);
        if (!danmaku.is(MessageType::MSG_DANMAKU))
            continue;
        if (!us->useStringSimilar)
        {
            if (danmaku.getText() == s)
                return true;
        }
        else
        {
            if (StringDistanceUtil::getSimilarity(danmaku.getText(), s) >= us->stringSimilarThreshold)
                return true;
        }
        if (--count <= 0)
            break;
    }
    return false;
}

bool MainWindow::isLiving() const
{
    return ac->liveStatus == 1;
}

void MainWindow::localNotify(const QString &text)
{
	if (text.isEmpty())
    {
        qWarning() << ">localNotify([空文本])";
        return ;
    }
    appendNewLiveDanmaku(LiveDanmaku(text));
}

void MainWindow::localNotify(const QString &text, qint64 uid)
{
    LiveDanmaku danmaku(text);
    danmaku.setUid(uid);
    appendNewLiveDanmaku(danmaku);
}

/**
 * 发送单条弹幕的原子操作
 */
void MainWindow::sendMsg(QString msg)
{
    if (localDebug)
    {
        localNotify("发送弹幕 -> " + msg + "  (" + snum(msg.length()) + ")");
        return ;
    }

    sendRoomMsg(ac->roomId, msg);
}

void MainWindow::sendRoomMsg(QString roomId, QString msg)
{
    if (ac->browserCookie.isEmpty() || ac->browserData.isEmpty())
    {
        showError("发送弹幕", "机器人账号未登录");
#ifdef ZUOQI_ENTRANCE
        QMessageBox::warning(this, "发送弹幕", "请点击登录按钮，登录机器人账号方可发送弹幕");
#endif
        return ;
    }
    if (msg.isEmpty() || roomId.isEmpty())
        return ;

    // 设置数据（JSON的ByteArray）
    QString s = ac->browserData;
    int posl = s.indexOf("msg=")+4;
    int posr = s.indexOf("&", posl);
    if (posr == -1)
        posr = s.length();
    s.replace(posl, posr-posl, msg);

    posl = s.indexOf("roomid=")+7;
    posr = s.indexOf("&", posl);
    if (posr == -1)
        posr = s.length();
    s.replace(posl, posr-posl, roomId);

    QByteArray ba(s.toStdString().data());

    // 连接槽
    post("https://api.live.bilibili.com/msg/send", ba, [=](QJsonObject json){
        QString errorMsg = json.value("message").toString();
        statusLabel->setText("");
        if (!errorMsg.isEmpty())
        {
            QString errorDesc = errorMsg;
            if (errorMsg == "f")
            {
                errorDesc = "包含屏蔽词";
            }
            else if (errorMsg == "k")
            {
                errorDesc = "包含直播间屏蔽词";
            }

            showError("发送弹幕失败", errorDesc);
            qWarning() << msg;
            localNotify(errorDesc + " -> " + msg);

            // 重试
            if (!ui->retryFailedDanmuCheck->isChecked())
                return ;

            if (errorMsg.contains("msg in 1s"))
            {
                localNotify("[5s后重试]");
                sendAutoMsgInFirst(msg, LiveDanmaku().withRetry().withRoomId(roomId), 5000);
            }
            else if (errorMsg.contains("msg repeat") || errorMsg.contains("频率过快"))
            {
                localNotify("[4s后重试]");
                sendAutoMsgInFirst(msg, LiveDanmaku().withRetry().withRoomId(roomId), 4200);
            }
            else if (errorMsg.contains("超出限制长度"))
            {
                if (msg.length() <= ui->danmuLongestSpin->value())
                {
                    localNotify("[错误的弹幕长度：" + snum(msg.length()) + "字，但设置长度" + snum(ui->danmuLongestSpin->value()) + "]");
                }
                else
                {
                    localNotify("[自动分割长度]");
                    sendAutoMsgInFirst(splitLongDanmu(msg).join("\\n"), LiveDanmaku().withRetry().withRoomId(roomId), 1000);
                }
            }
            else if (errorMsg == "f") // 系统敏感词
            {
            }
            else if (errorMsg == "k") // 主播设置的直播间敏感词
            {
            }
        }
    });
}

/**
 * 发送带有变量的弹幕或者执行代码
 * @param msg       多行，带变量
 * @param danmaku   参数
 * @param channel   通道
 * @param manual    是否手动
 * @param delayMine 自己通过弹幕触发的，执行需要晚一点
 * @return          是否有发送弹幕或执行代码
 */
bool MainWindow::sendVariantMsg(QString msg, const LiveDanmaku &danmaku, int channel, bool manual, bool delayMine)
{
    QStringList msgs = getEditConditionStringList(msg, danmaku);
    if (!msgs.size())
        return false;

    int r = qrand() % msgs.size();
    QString s = msgs.at(r);
    if (!s.trimmed().isEmpty())
    {
        if (delayMine && QString::number(danmaku.getUid()) == ac->cookieUid) // 自己发的，自己回复，必须要延迟一会儿
        {
            if (s.contains(QRegExp("cd\\d+\\s*:\\s*\\d+"))) // 带冷却通道，不能放前面
                autoMsgTimer->start(); // 先启动，避免立即发送
            else
                s = "\\n" + s; // 延迟一次发送的时间
        }
        sendCdMsg(s, danmaku, 0, channel, true, false, manual);
    }
    return true;
}

/**
 * 发送多条消息
 * 不允许包含变量
 * 使用“\n”进行多行换行
 */
void MainWindow::sendAutoMsg(QString msgs, const LiveDanmaku &danmaku)
{
    if (msgs.trimmed().isEmpty())
    {
        if (debugPrint)
            localNotify("[空弹幕，已忽略]");
        return ;
    }

    // 发送前替换
    for (auto it = us->replaceVariant.begin(); it != us->replaceVariant.end(); ++it)
    {
        msgs.replace(QRegularExpression(it->first), it->second);
    }

    // 分割与发送
    QStringList sl = msgs.split("\\n", QString::SkipEmptyParts);
    autoMsgQueues.append(qMakePair(sl, danmaku));
    if (!autoMsgTimer->isActive() && !inDanmakuDelay)
    {
        slotSendAutoMsg(false); // 先运行一次
    }
    if (!autoMsgTimer->isActive())
        autoMsgTimer->start();
}

void MainWindow::sendAutoMsgInFirst(QString msgs, const LiveDanmaku &danmaku, int interval)
{
    if (msgs.trimmed().isEmpty())
    {
        if (debugPrint)
            localNotify("[空弹幕，已忽略]");
        return ;
    }
    QStringList sl = msgs.split("\\n", QString::SkipEmptyParts);
    autoMsgQueues.insert(0, qMakePair(sl, danmaku));
    if (interval > 0)
    {
        autoMsgTimer->setInterval(interval);
    }
    autoMsgTimer->stop();
    autoMsgTimer->start();
    /* if (!autoMsgTimer->isActive())
    {
        autoMsgTimer->start();
    } */
}

/**
 * 执行发送队列中的发送弹幕，或者命令操作
 * 不允许包括变量
 * // @return 是否是执行命令。为空或发送弹幕为false
 */
void MainWindow::slotSendAutoMsg(bool timeout)
{
    if (timeout) // 全部发完之后 timer 一定还开着的，最后一次 timeout 清除弹幕发送冷却
        inDanmakuCd = false;

    if (autoMsgQueues.isEmpty())
    {
        autoMsgTimer->stop();
        return ;
    }

    if (autoMsgTimer->interval() != AUTO_MSG_CD) // 之前命令修改过延时
        autoMsgTimer->setInterval(AUTO_MSG_CD);

    if (!autoMsgQueues.first().first.size()) // 有一条空消息，应该是哪里添加错了的
    {
        qWarning() << "错误的发送消息：空消息，已跳过";
        autoMsgQueues.removeFirst();
        slotSendAutoMsg(false);
        return ;
    }

    QStringList* sl = &autoMsgQueues[0].first;
    LiveDanmaku* danmaku = &autoMsgQueues[0].second;
    QString msg = sl->takeFirst();

    if (sl->isEmpty()) // 消息发送完了
    {
        danmaku = new LiveDanmaku(autoMsgQueues[0].second);
        sl = new QStringList;
        autoMsgQueues.removeFirst(); // 此时 danmaku 对象也有已经删掉了
        QTimer::singleShot(0, [=]{
            delete sl;
            delete danmaku;
        });
    }
    if (!autoMsgQueues.size())
        autoMsgTimer->stop();

    CmdResponse res = NullRes;
    int resVal = 0;
    bool exec = execFunc(msg, *danmaku, res, resVal);

    if (!exec) // 如果是发送弹幕
    {    
        msg = msgToShort(msg);
        addNoReplyDanmakuText(msg);
        if (danmaku->isRetry())
            sendRoomMsg(danmaku->getRoomId(), msg);
        else
            sendMsg(msg);
        inDanmakuCd = true;
        us->setValue("danmaku/robotTotalSend", ++robotTotalSendMsg);
        ui->robotSendCountLabel->setText(snum(robotTotalSendMsg));
        ui->robotSendCountLabel->setToolTip("累计发送弹幕 " + snum(robotTotalSendMsg) + " 条");
    }
    else // 是执行命令，发送下一条弹幕就不需要延迟了
    {
        if (res == AbortRes) // 终止这一轮后面的弹幕
        {
            if (!sl->isEmpty()) // 如果为空，则自动为终止
                autoMsgQueues.removeFirst();
            return ;
        }
        else if (res == DelayRes) // 修改延迟
        {
            if (resVal < 0)
                qCritical() << "设置延时时间出错";
            autoMsgTimer->setInterval(resVal);
            if (!autoMsgTimer->isActive())
                autoMsgTimer->start();
            inDanmakuDelay = true;
            QTimer::singleShot(resVal, [=]{
                inDanmakuDelay = false;
            });
            return ;
        }
    }

    // 如果后面是命令的话，尝试立刻执行
    // 可以取消间隔，大大加快速度
    // 因为大部分复杂的代码，也就是一条弹幕+一堆命令
    if (!inDanmakuDelay && autoMsgQueues.size())
    {
        const QString& nextMsg = autoMsgQueues.first().first.first();
        QRegularExpression re("^\\s*(>.+\\)|\\{.+\\}.*[=\\+\\-].*)\\s*$");
        if (nextMsg.indexOf(re) > -1) // 下一条是命令，直接执行
        {
            slotSendAutoMsg(false); // 递归
        }
    }
}

/**
 * 发送前确保没有需要调整的变量了
 * 若有变量，请改用：sendVariantMsg
 * 而且已经不需要在意条件了
 * 一般解析后的弹幕列表都随机执行一行，以cd=0来发送cd弹幕
 * 带有cd channel，但实际上不一定有cd
 * @param msg 单行要发送的弹幕序列
 */
void MainWindow::sendCdMsg(QString msg, LiveDanmaku danmaku, int cd, int channel, bool enableText, bool enableVoice, bool manual)
{
    if (!manual && !shallAutoMsg()) // 不在直播中
    {
        qInfo() << "未开播，不做操作(cd)" << msg;
        if (debugPrint)
            localNotify("[未开播，不做操作]");
        return ;
    }
    if (msg.trimmed().isEmpty())
    {
        if (debugPrint)
            localNotify("[空弹幕，已跳过]");
        return ;
    }

    bool forceAdmin = false;
    int waitCount = 0;
    int waitChannel = 0;

    // 弹幕选项
    QRegularExpression re("^\\s*\\(([\\w\\d:, ]+)\\)");
    QRegularExpressionMatch match;
    if (msg.indexOf(re, 0, &match) > -1)
    {
        QStringList caps = match.capturedTexts();
        QString full = caps.at(0);
        QString optionStr = caps.at(1);
        QStringList options = optionStr.split(",", QString::SkipEmptyParts);

        // 遍历每一个选项
        foreach (auto option, options)
        {
            option = option.trimmed();

            // 冷却通道
            re.setPattern("^cd(\\d{1,2})\\s*:\\s*(\\d+)$");
            if (option.indexOf(re, 0, &match) > -1)
            {
                caps = match.capturedTexts();
                QString chann = caps.at(1);
                QString val = caps.at(2);
                channel = chann.toInt();
                cd = val.toInt() * 1000; // 秒转毫秒
                continue;
            }

            // 等待通道
            re.setPattern("^wait(\\d{1,2})\\s*:\\s*(\\d+)$");
            if (option.indexOf(re, 0, &match) > -1)
            {
                caps = match.capturedTexts();
                QString chann = caps.at(1);
                QString val = caps.at(2);
                waitChannel = chann.toInt();
                waitCount = val.toInt();
                continue;
            }

            // 强制房管权限
            re.setPattern("^admin(\\s*[=:]\\s*(true|1))$");
            if (option.contains(re))
            {
                forceAdmin = true;
                continue;
            }
        }

        // 去掉选项，发送剩余弹幕
        msg = msg.right(msg.length() - full.length());
    }

    // 冷却通道：避免太频繁发消息
    /* analyzeMsgAndCd(msg, cd, channel); */
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch(); // 值是毫秒，用户配置为秒
    if (cd > 0 && timestamp - msgCds[channel] < cd)
    {
        if (debugPrint)
            localNotify("[未完成冷却：" + snum(timestamp - msgCds[channel]) + " 不足 " + snum(cd) + "毫秒]");
        return ;
    }
    msgCds[channel] = timestamp; // 重新冷却

    // 等待通道
    if (waitCount > 0 && msgWaits[waitChannel] < waitCount)
    {
        if (debugPrint)
            localNotify("[未完成等待：" + snum(msgWaits[waitChannel]) + " 不足 " + snum(waitCount) + "条]");
        return ;
    }
    msgWaits[waitChannel] = 0; // 重新等待

    // 强制房管权限
    if (forceAdmin)
        danmaku.setAdmin(true);

    if (enableText)
    {
        sendAutoMsg(msg, danmaku);
    }
    if (enableVoice)
        speekVariantText(msg);
}

void MainWindow::sendCdMsg(QString msg, const LiveDanmaku &danmaku)
{
    sendCdMsg(msg, danmaku, 0, NOTIFY_CD_CN, true, false, false);
}

void MainWindow::sendGiftMsg(QString msg, const LiveDanmaku &danmaku)
{
    sendCdMsg(msg, danmaku, ui->sendGiftCDSpin->value() * 1000, GIFT_CD_CN,
              ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked(), false);
}

void MainWindow::sendAttentionMsg(QString msg, const LiveDanmaku &danmaku)
{
    sendCdMsg(msg, danmaku, ui->sendAttentionCDSpin->value() * 1000, GIFT_CD_CN,
              ui->sendAttentionTextCheck->isChecked(), ui->sendAttentionVoiceCheck->isChecked(), false);
}

void MainWindow::sendNotifyMsg(QString msg, bool manual)
{
    sendCdMsg(msg, LiveDanmaku(), NOTIFY_CD, NOTIFY_CD_CN,
              true, false, manual);
}

void MainWindow::sendNotifyMsg(QString msg, const LiveDanmaku &danmaku, bool manual)
{
    sendCdMsg(msg, danmaku, NOTIFY_CD, NOTIFY_CD_CN,
              true, false, manual);
}

/**
 * 连击定时器到达
 */
void MainWindow::slotComboSend()
{
    if (!giftCombos.size())
    {
        comboTimer->stop();
        return ;
    }

    qint64 timestamp = QDateTime::currentSecsSinceEpoch();
    int delta = ui->giftComboDelaySpin->value();

    auto thankGift = [=](LiveDanmaku danmaku) -> bool {
        QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
        if (words.size())
        {
            int r = qrand() % words.size();
            QString msg = words.at(r);
            if (us->strongNotifyUsers.contains(danmaku.getUid()))
            {
                if (debugPrint)
                    localNotify("[强提醒]");
                sendCdMsg(msg, danmaku, NOTIFY_CD, GIFT_CD_CN,
                          ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked(), false);
            }
            else
            {
                sendGiftMsg(msg, danmaku);
            }
            return true;
        }
        else if (debugPrint)
        {
            localNotify("[没有可发送的连击答谢弹幕]");
            return false;
        }
        return false;
    };

    if (!ui->giftComboTopCheck->isChecked()) // 全部答谢，看冷却时间
    {
        for (auto it = giftCombos.begin(); it != giftCombos.end(); )
        {
            const LiveDanmaku& danmaku = it.value();
            if (danmaku.getTimeline().toSecsSinceEpoch() + delta > timestamp) // 未到达统计时间
            {
                it++;
                continue;
            }

            // 开始答谢该项
            thankGift(danmaku);
            it = giftCombos.erase(it);
        }
    }
    else // 等待结束，只答谢最贵的一项
    {
        qint64 maxGold = 0;
        auto maxIt = giftCombos.begin();
        for (auto it = giftCombos.begin(); it != giftCombos.end(); it++)
        {
            const LiveDanmaku& danmaku = it.value();
            if (danmaku.getTimeline().toSecsSinceEpoch() + delta > timestamp) // 还有礼物未到答谢时间
                return ;
            if (danmaku.isGoldCoin() && danmaku.getTotalCoin() > maxGold)
            {
                maxGold = danmaku.getTotalCoin();
                maxIt = it;
            }
        }
        if (maxGold > 0)
        {
            thankGift(maxIt.value());
            giftCombos.clear();
            return ;
        }

        // 没有金瓜子，只有银瓜子
        int maxSilver = 0;
        maxIt = giftCombos.begin();
        for (auto it = giftCombos.begin(); it != giftCombos.end(); it++)
        {
            const LiveDanmaku& danmaku = it.value();
            if (danmaku.getTotalCoin() > maxGold)
            {
                maxSilver = danmaku.getTotalCoin();
                maxIt = it;
            }
        }
        thankGift(maxIt.value());
        giftCombos.clear();
        return ;
    }

}

void MainWindow::on_DiangeAutoCopyCheck_stateChanged(int)
{
    us->setValue("danmaku/diangeAutoCopy", diangeAutoCopy = ui->DiangeAutoCopyCheck->isChecked());
}

void MainWindow::on_testDanmakuButton_clicked()
{
    QString text = ui->testDanmakuEdit->text();
    if (text.isEmpty())
        text = "测试弹幕";
    QRegularExpressionMatch match;

    qint64 uid = 123;
    int r = qrand() % 7 + 1;
    if (text.startsWith("$"))
    {
        text.replace(0, 1, "");
        uid = ac->cookieUid.toLongLong();
    }
    else
    {
        uid = 10000 + r;
    }

    if (text == "赠送吃瓜")
    {
        sendGift(20004, 1);
    }
    else if (text == "测试送礼")
    {
        QString username = "测试用户";
        QString giftName = "测试礼物";
        int giftId = 123;
        int num = qrand() % 3 + 1;
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        QString coinType = qrand()%2 ? "gold" : "silver";
        int totalCoin = qrand() % 20 * 100;
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        appendNewLiveDanmaku(danmaku);
        if (ui->saveEveryGiftCheck->isChecked())
            saveEveryGift(danmaku);

        QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
        qInfo() << "条件替换结果：" << words;

        triggerCmdEvent("SEND_GIFT", danmaku, true);
    }
    else if (text == "测试礼物连击")
    {
        QString username = "测试用户";
        QString giftName = "测试礼物";
        int giftId = 123;
        int num = qrand() % 3 + 1;
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        QString coinType = qrand()%2 ? "gold" : "silver";
        int totalCoin = qrand() % 20 * 100;
        uid = 123456;
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);

        bool merged = mergeGiftCombo(danmaku); // 如果有合并，则合并到之前的弹幕上面
        if (!merged)
        {
            appendNewLiveDanmaku(danmaku);
        }
    }
    else if (text == "测试醒目留言")
    {
        LiveDanmaku danmaku("测试用户", "测试弹幕", 1001, 12, QDateTime::currentDateTime(), "", "", 39, "醒目留言", 1, 30);
        appendNewLiveDanmaku(danmaku);
    }
    else if (text == "测试消息")
    {
        localNotify("测试通知消息");
    }
    else if (text == "测试舰长进入")
    {
        QString uname = "测试舰长";
        LiveDanmaku danmaku(qrand() % 3 + 1, uname, uid, QDateTime::currentDateTime());
        appendNewLiveDanmaku(danmaku);

        if (ui->autoSendWelcomeCheck->isChecked() && !us->notWelcomeUsers.contains(uid))
        {
            sendWelcome(danmaku);
        }
    }
    else if (text == "测试舰长")
    {
        QString username = "测试用户";
        QString giftName = "舰长";
        int num = qrand() % 3 + 1;
        int gift_id = 10003;
        int guard_level = 3;
        int price = 198000;
        int rg = qrand() % 6;
        if (rg < 3)
        {
            giftName = "舰长";
            guard_level = 3;
            price = 198000;
        }
        else if (rg < 5)
        {
            giftName = "提督";
            guard_level = 2;
            price = 1980000;
        }
        else
        {
            giftName = "总督";
            guard_level = 1;
            price = 19800000;
        }
        gift_id = 10000 + guard_level;
        LiveDanmaku danmaku(username, uid, giftName, num, guard_level, gift_id, price, 2);
        appendNewLiveDanmaku(danmaku);
        appendLiveGuard(danmaku);
        if (ui->saveEveryGuardCheck->isChecked())
            saveEveryGuard(danmaku);

        if (!justStart && ui->autoSendGiftCheck->isChecked())
        {
            QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
            if (words.size())
            {
                int r = qrand() % words.size();
                QString msg = words.at(r);
                sendCdMsg(msg, danmaku, NOTIFY_CD, NOTIFY_CD_CN,
                          ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked(), false);
            }
            else if (debugPrint)
            {
                localNotify("[没有可发送的上船弹幕]");
            }
        }
    }
    else if (text.startsWith(">"))
    {
        processRemoteCmd(text.replace(0, 1, ""));
    }
    else if (text == "测试开播")
    {
        QString text = ui->startLiveWordsEdit->text();
        if (ui->startLiveSendCheck->isChecked() && !text.trimmed().isEmpty())
            sendAutoMsg(text, LiveDanmaku());
        ui->liveStatusButton->setText("已开播");
        ac->liveStatus = 1;
        if (ui->timerConnectServerCheck->isChecked() && connectServerTimer->isActive())
            connectServerTimer->stop();
        slotStartWork(); // 每个房间第一次开始工作

        triggerCmdEvent("LIVE", LiveDanmaku(), true);
    }
    else if (text == "测试对面主播进入")
    {
        QJsonObject json;
        json.insert("cmd", "INTERACT_WORD");
        QJsonObject data;
        data.insert("msg_type", 1);
        data.insert("timestamp", QDateTime::currentSecsSinceEpoch());
        data.insert("uid", pkUid.toLongLong());
        data.insert("uname", "测试用户");
        json.insert("data", data);
        handleMessage(json);
        qInfo() << pkUid << oppositeAudience;
    }
    else if (text == "测试花灯")
    {
        QString username = "测试用户";
        QString giftName = "炫彩花灯";
        int r = qrand() % 5;
        if (r == 1)
            giftName = "漫天花灯";
        else if (r == 2)
            giftName = "暴富花灯";
        int giftId = 123;
        int num = qrand() % 3 + 1;
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        QString coinType = qrand()%2 ? "gold" : "silver";
        int totalCoin = qrand() % 20 * 100;
        uid = 123456;
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        appendNewLiveDanmaku(danmaku);
        triggerCmdEvent("SEND_GIFT", danmaku, true);
    }
    else if (text == "测试高能榜")
    {
        QByteArray ba = "{ \"cmd\": \"ENTRY_EFFECT\", \"data\": { \"basemap_url\": \"https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png\", \"business\": 3, \"copy_color\": \"#000000\", \"copy_writing\": \"欢迎 <%___君陌%> 进入直播间\", \"copy_writing_v2\": \"欢迎 <^icon^> <%___君陌%> 进入直播间\", \"effective_time\": 2, \"face\": \"https://i1.hdslb.com/bfs/face/8fb8336e1ae50001ca76b80c30b01d23b07203c9.jpg\", \"highlight_color\": \"#FFF100\", \"icon_list\": [ 2 ], \"id\": 136, \"max_delay_time\": 7, \"mock_effect\": 0, \"priority\": 1, \"privilege_type\": 0, \"show_avatar\": 1, \"target_id\": 5988102, \"uid\": 453364, \"web_basemap_url\": \"https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png\", \"web_close_time\": 900, \"web_effect_close\": 0, \"web_effective_time\": 2 } }";
        handleMessage(QJsonDocument::fromJson(ba).object());
    }
    else if (text == "测试对面信息")
    {
        getPkMatchInfo();
    }
    else if (text == "测试对面舰长")
    {
        getPkOnlineGuardPage(0);
    }
    else if (text == "测试偷塔")
    {
        execTouta();
    }
    else if (text == "测试心跳")
    {
        if (xliveHeartBeatBenchmark.isEmpty())
            sendXliveHeartBeatE();
        else
            sendXliveHeartBeatX();
    }
    else if (text == "测试对面信息")
    {
        getPkMatchInfo();
    }
    else if (text == "测试断线")
    {
        socket->close();
    }
    else if (text == "爆出礼物")
    {
        QByteArray ba = "{\
                        \"cmd\": \"SEND_GIFT\",\
                        \"data\": {\
                            \"anchor_roomid\": \"0\",\
                            \"coin_type\": \"gold\",\
                            \"extra\": {\
                                \"action\": \"投喂\",\
                                \"batch_combo_id\": \"f1100ee9-9068-4122-808d-2f4c7ebd5143\",\
                                \"batch_combo_send\": {\
                                    \"action\": \"投喂\",\
                                    \"batch_combo_id\": \"f1100ee9-9068-4122-808d-2f4c7ebd5143\",\
                                    \"batch_combo_num\": 1,\
                                    \"blind_gift\": {\
                                        \"blind_gift_config_id\": 27,\
                                        \"gift_action\": \"爆出\",\
                                        \"original_gift_id\": 31026,\
                                        \"original_gift_name\": \"白银宝盒\"\
                                    },\
                                    \"gift_id\": 20011,\
                                    \"gift_name\": \"金币\",\
                                    \"gift_num\": 1,\
                                    \"send_master\": null,\
                                    \"uid\": 4303190,\
                                    \"uname\": \"舞月雅白\"\
                                },\
                                \"beatId\": \"\",\
                                \"biz_source\": \"Live\",\
                                \"blind_gift\": {\
                                    \"blind_gift_config_id\": 27,\
                                    \"gift_action\": \"爆出\",\
                                    \"original_gift_id\": 31026,\
                                    \"original_gift_name\": \"白银宝盒\"\
                                },\
                                \"broadcast_id\": 0,\
                                \"coin_type\": \"gold\",\
                                \"combo_resources_id\": 1,\
                                \"combo_send\": {\
                                    \"action\": \"投喂\",\
                                    \"combo_id\": \"4fa537d8-abf7-4e28-8f74-581ea050fde1\",\
                                    \"combo_num\": 1,\
                                    \"gift_id\": 20011,\
                                    \"gift_name\": \"金币\",\
                                    \"gift_num\": 1,\
                                    \"send_master\": null,\
                                    \"uid\": 4303190,\
                                    \"uname\": \"舞月雅白\"\
                                },\
                                \"combo_stay_time\": 3,\
                                \"combo_total_coin\": 600,\
                                \"crit_prob\": 0,\
                                \"demarcation\": 1,\
                                \"discount_price\": 600,\
                                \"dmscore\": 112,\
                                \"draw\": 0,\
                                \"effect\": 0,\
                                \"effect_block\": 0,\
                                \"face\": \"http://i2.hdslb.com/bfs/face/bdd1084ccdd3ecc55969a0ac4dd9be588ad1959a.jpg\",\
                                \"float_sc_resource_id\": 0,\
                                \"giftId\": 20011,\
                                \"giftName\": \"金币\",\
                                \"giftType\": 0,\
                                \"gold\": 0,\
                                \"guard_level\": 0,\
                                \"is_first\": true,\
                                \"is_special_batch\": 0,\
                                \"magnification\": 1,\
                                \"medal_info\": {\
                                    \"anchor_roomid\": 0,\
                                    \"anchor_uname\": \"\",\
                                    \"guard_level\": 0,\
                                    \"icon_id\": 0,\
                                    \"is_lighted\": 0,\
                                    \"medal_color\": 13081892,\
                                    \"medal_color_border\": 12632256,\
                                    \"medal_color_end\": 12632256,\
                                    \"medal_color_start\": 12632256,\
                                    \"medal_level\": 20,\
                                    \"medal_name\": \"东方\",\
                                    \"special\": \"\",\
                                    \"target_id\": 12836417\
                                },\
                                \"name_color\": \"\",\
                                \"num\": 1,\
                                \"original_gift_name\": \"\",\
                                \"price\": 600,\
                                \"rcost\": 282839,\
                                \"remain\": 0,\
                                \"rnd\": \"1097737974\",\
                                \"send_master\": null,\
                                \"silver\": 0,\
                                \"super\": 0,\
                                \"super_batch_gift_num\": 1,\
                                \"super_gift_num\": 1,\
                                \"svga_block\": 0,\
                                \"tag_image\": \"\",\
                                \"tid\": \"1636295428121000002\",\
                                \"timestamp\": 1636295428,\
                                \"top_list\": null,\
                                \"total_coin\": 1000,\
                                \"uid\": 4303190,\
                                \"uname\": \"舞月雅白\"\
                            },\
                            \"gift_id\": 20011,\
                            \"gift_name\": \"金币\",\
                            \"medal_color\": \"c79d24\",\
                            \"medal_level\": 20,\
                            \"medal_name\": \"东方\",\
                            \"medal_up\": \"\",\
                            \"msgType\": 2,\
                            \"nickname\": \"舞月雅白\",\
                            \"number\": 1,\
                            \"timeline\": \"2021-11-07 22:30:33\",\
                            \"total_coin\": 1000,\
                            \"uid\": 4303190\
                        }\
                    }";

        sendTextToSockets("SEND_GIFT", ba);
    }
    else if (text == "投喂银瓜子")
    {
        QByteArray ba = "{\
                        \"cmd\": \"SEND_GIFT\",\
                        \"data\": {\
                            \"anchor_roomid\": \"0\",\
                            \"coin_type\": \"silver\",\
                            \"extra\": {\
                                \"action\": \"投喂\",\
                                \"batch_combo_id\": \"\",\
                                \"batch_combo_send\": null,\
                                \"beatId\": \"\",\
                                \"biz_source\": \"Live\",\
                                \"blind_gift\": null,\
                                \"broadcast_id\": 0,\
                                \"coin_type\": \"silver\",\
                                \"combo_resources_id\": 1,\
                                \"combo_send\": null,\
                                \"combo_stay_time\": 3,\
                                \"combo_total_coin\": 0,\
                                \"crit_prob\": 0,\
                                \"demarcation\": 1,\
                                \"discount_price\": 0,\
                                \"dmscore\": 12,\
                                \"draw\": 0,\
                                \"effect\": 0,\
                                \"effect_block\": 1,\
                                \"face\": \"http://i0.hdslb.com/bfs/face/member/noface.jpg\",\
                                \"float_sc_resource_id\": 0,\
                                \"giftId\": 1,\
                                \"giftName\": \"辣条\",\
                                \"giftType\": 5,\
                                \"gold\": 0,\
                                \"guard_level\": 0,\
                                \"is_first\": true,\
                                \"is_special_batch\": 0,\
                                \"magnification\": 1,\
                                \"medal_info\": {\
                                    \"anchor_roomid\": 0,\
                                    \"anchor_uname\": \"\",\
                                    \"guard_level\": 0,\
                                    \"icon_id\": 0,\
                                    \"is_lighted\": 1,\
                                    \"medal_color\": 6126494,\
                                    \"medal_color_border\": 6126494,\
                                    \"medal_color_end\": 6126494,\
                                    \"medal_color_start\": 6126494,\
                                    \"medal_level\": 7,\
                                    \"medal_name\": \"戒不掉\",\
                                    \"special\": \"\",\
                                    \"target_id\": 300702024\
                                },\
                                \"name_color\": \"\",\
                                \"num\": 1,\
                                \"original_gift_name\": \"\",\
                                \"price\": 100,\
                                \"rcost\": 292387,\
                                \"remain\": 0,\
                                \"rnd\": \"AD3B6A47-5D22-45F3-8340-7C4A20B5DA53\",\
                                \"send_master\": null,\
                                \"silver\": 0,\
                                \"super\": 0,\
                                \"super_batch_gift_num\": 0,\
                                \"super_gift_num\": 0,\
                                \"svga_block\": 0,\
                                \"tag_image\": \"\",\
                                \"tid\": \"1636639739130000002\",\
                                \"timestamp\": 1636639739,\
                                \"top_list\": null,\
                                \"total_coin\": 100,\
                                \"uid\": 702014570,\
                                \"uname\": \"花冢㐅\"\
                            },\
                            \"gift_id\": 1,\
                            \"gift_name\": \"辣条\",\
                            \"medal_color\": \"5d7b9e\",\
                            \"medal_level\": 7,\
                            \"medal_name\": \"戒不掉\",\
                            \"medal_up\": \"\",\
                            \"msgType\": 2,\
                            \"nickname\": \"花冢㐅\",\
                            \"number\": 1,\
                            \"timeline\": \"2021-11-11 22:08:59\",\
                            \"total_coin\": 100,\
                            \"uid\": 702014570\
                        }\
                    }";
        sendTextToSockets("SEND_GIFT", ba);
    }
    else if (text == "投喂电池")
    {
        QByteArray ba = "{\
                        \"cmd\": \"SEND_GIFT\",\
                        \"data\": {\
                            \"anchor_roomid\": \"0\",\
                            \"coin_type\": \"gold\",\
                            \"extra\": {\
                                \"action\": \"投喂\",\
                                \"batch_combo_id\": \"batch:gift:combo_id:37794207:113579884:31039:1636639828.0140\",\
                                \"batch_combo_send\": {\
                                    \"action\": \"投喂\",\
                                    \"batch_combo_id\": \"batch:gift:combo_id:37794207:113579884:31039:1636639828.0140\",\
                                    \"batch_combo_num\": 1,\
                                    \"blind_gift\": null,\
                                    \"gift_id\": 31039,\
                                    \"gift_name\": \"牛哇牛哇\",\
                                    \"gift_num\": 1,\
                                    \"send_master\": null,\
                                    \"uid\": 37794207,\
                                    \"uname\": \"雷神专用柒柒\"\
                                },\
                                \"beatId\": \"\",\
                                \"biz_source\": \"Live\",\
                                \"blind_gift\": null,\
                                \"broadcast_id\": 0,\
                                \"coin_type\": \"gold\",\
                                \"combo_resources_id\": 1,\
                                \"combo_send\": {\
                                    \"action\": \"投喂\",\
                                    \"combo_id\": \"gift:combo_id:37794207:113579884:31039:1636639828.0131\",\
                                    \"combo_num\": 1,\
                                    \"gift_id\": 31039,\
                                    \"gift_name\": \"牛哇牛哇\",\
                                    \"gift_num\": 1,\
                                    \"send_master\": null,\
                                    \"uid\": 37794207,\
                                    \"uname\": \"雷神专用柒柒\"\
                                },\
                                \"combo_stay_time\": 3,\
                                \"combo_total_coin\": 100,\
                                \"crit_prob\": 0,\
                                \"demarcation\": 1,\
                                \"discount_price\": 100,\
                                \"dmscore\": 120,\
                                \"draw\": 0,\
                                \"effect\": 0,\
                                \"effect_block\": 0,\
                                \"face\": \"http://i0.hdslb.com/bfs/face/eb101ef90ebc4e9bf79f65312a22ebac84946700.jpg\",\
                                \"float_sc_resource_id\": 0,\
                                \"giftId\": 31039,\
                                \"giftName\": \"牛哇牛哇\",\
                                \"giftType\": 0,\
                                \"gold\": 0,\
                                \"guard_level\": 3,\
                                \"is_first\": true,\
                                \"is_special_batch\": 0,\
                                \"magnification\": 1,\
                                \"medal_info\": {\
                                    \"anchor_roomid\": 0,\
                                    \"anchor_uname\": \"\",\
                                    \"guard_level\": 3,\
                                    \"icon_id\": 0,\
                                    \"is_lighted\": 1,\
                                    \"medal_color\": 1725515,\
                                    \"medal_color_border\": 6809855,\
                                    \"medal_color_end\": 5414290,\
                                    \"medal_color_start\": 1725515,\
                                    \"medal_level\": 21,\
                                    \"medal_name\": \"女仆厨\",\
                                    \"special\": \"\",\
                                    \"target_id\": 113579884\
                                },\
                                \"name_color\": \"#00D1F1\",\
                                \"num\": 1,\
                                \"original_gift_name\": \"\",\
                                \"price\": 100,\
                                \"rcost\": 292391,\
                                \"remain\": 0,\
                                \"rnd\": \"180269212\",\
                                \"send_master\": null,\
                                \"silver\": 0,\
                                \"super\": 0,\
                                \"super_batch_gift_num\": 1,\
                                \"super_gift_num\": 1,\
                                \"svga_block\": 0,\
                                \"tag_image\": \"\",\
                                \"tid\": \"1636639827121700001\",\
                                \"timestamp\": 1636639827,\
                                \"top_list\": null,\
                                \"total_coin\": 100,\
                                \"uid\": 37794207,\
                                \"uname\": \"雷神专用柒柒\"\
                            },\
                            \"gift_id\": 31039,\
                            \"gift_name\": \"牛哇牛哇\",\
                            \"medal_color\": \"1a544b\",\
                            \"medal_level\": 21,\
                            \"medal_name\": \"女仆厨\",\
                            \"medal_up\": \"\",\
                            \"msgType\": 2,\
                            \"nickname\": \"雷神专用柒柒\",\
                            \"number\": 1,\
                            \"timeline\": \"2021-11-11 22:10:29\",\
                            \"total_coin\": 100,\
                            \"uid\": 37794207\
                        }\
                    }";
        sendTextToSockets("SEND_GIFT", ba);
    }
    else if (text == "礼物连击")
    {
        QByteArray ba = "{\
                        \"cmd\": \"COMBO_SEND\",\
                        \"data\": {\
                            \"extra\": {\
                                \"action\": \"投喂\",\
                                \"batch_combo_id\": \"batch:gift:combo_id:14729370:113579884:30869:1637386968.9309\",\
                                \"batch_combo_num\": 2,\
                                \"combo_id\": \"gift:combo_id:14729370:113579884:30869:1637386968.9301\",\
                                \"combo_num\": 200,\
                                \"combo_total_coin\": 20000,\
                                \"dmscore\": 112,\
                                \"gift_id\": 30869,\
                                \"gift_name\": \"心动卡\",\
                                \"gift_num\": 100,\
                                \"is_show\": 1,\
                                \"medal_info\": {\
                                    \"anchor_roomid\": 0,\
                                    \"anchor_uname\": \"\",\
                                    \"guard_level\": 0,\
                                    \"icon_id\": 0,\
                                    \"is_lighted\": 1,\
                                    \"medal_color\": 6067854,\
                                    \"medal_color_border\": 6067854,\
                                    \"medal_color_end\": 6067854,\
                                    \"medal_color_start\": 6067854,\
                                    \"medal_level\": 4,\
                                    \"medal_name\": \"绒冰球\",\
                                    \"special\": \"\",\
                                    \"target_id\": 21374533\
                                },\
                                \"name_color\": \"\",\
                                \"r_uname\": \"水良子\",\
                                \"ruid\": 113579884,\
                                \"send_master\": null,\
                                \"total_num\": 200,\
                                \"uid\": 14729370,\
                                \"uname\": \"半夏惜沫\"\
                            },\
                            \"msgType\": 0,\
                            \"nickname\": \"\",\
                            \"timeline\": \"\",\
                            \"uid\": 0\
                        }\
                    }";
        sendTextToSockets("COMBO_SEND", ba);
    }
    else if (text == "开启互动玩法")
    {
        liveOpenService->start();
    }
    else if (text == "关闭互动玩法")
    {
        liveOpenService->end();
    }
    else if (text == "昵称简化测试")
    {
        auto query = sqlService.getQuery("select distinct uname from interact where msg_type = 1");
        QStringList sl;
        while (query.next())
        {
            QString uname = query.value(0).toString();
            QString ainame = nicknameSimplify(LiveDanmaku(uname, 0));
            sl.append(uname + "," +ainame);
        }
        writeTextFile("昵称简化测试.csv", sl.join("\n"));
    }
    else if (text == "测试每月")
    {
        triggerCmdEvent("NEW_DAY", LiveDanmaku());
        triggerCmdEvent("NEW_DAY_FIRST", LiveDanmaku());
        triggerCmdEvent("NEW_MONTH", LiveDanmaku());
        triggerCmdEvent("NEW_MONTH_FIRST", LiveDanmaku());
    }
    else if (text == "测试崩溃")
    {
        int* p = nullptr;
        *p = 1;
    }
    else
    {
        appendNewLiveDanmaku(LiveDanmaku("测试用户" + QString::number(r), text,
                             uid, 12,
                             QDateTime::currentDateTime(), "", ""));
    }
}

void MainWindow::on_removeDanmakuIntervalSpin_valueChanged(int arg1)
{
    this->removeDanmakuInterval = arg1 * 1000;
    us->setValue("danmaku/removeInterval", arg1);
}

void MainWindow::on_roomIdEdit_editingFinished()
{
    if (ac->roomId == ui->roomIdEdit->text() || ac->shortId == ui->roomIdEdit->text())
        return ;

    // 关闭旧的
    if (socket)
    {
        ac->liveStatus = 0;
        if (socket->state() != QAbstractSocket::UnconnectedState)
            socket->abort();
    }
    ac->roomId = ui->roomIdEdit->text();
    ac->upUid = "";
    us->setValue("danmaku/roomId", ac->roomId);

    releaseLiveData();

    // 判断是身份码还是房间号
    bool isPureRoomId = (ac->roomId.contains(QRegularExpression("^\\d+$")));

    // 先解析身份码
    if (!isPureRoomId)
    {
        ac->identityCode = ac->roomId;
        ui->identityCodeEdit->setText(ac->identityCode);
        us->set("live-open/identityCode", ac->identityCode);
        startConnectIdentityCode();
        return ;
    }

    // 直接继续
    emit signalRoomChanged(ac->roomId);

    // 开启新的
    if (socket)
    {
        startConnectRoom();
    }
    pullLiveDanmaku();
}

void MainWindow::on_languageAutoTranslateCheck_stateChanged(int)
{
    auto trans = ui->languageAutoTranslateCheck->isChecked();
    us->setValue("danmaku/autoTrans", trans);
    if (danmakuWindow)
        danmakuWindow->setAutoTranslate(trans);
}

void MainWindow::on_tabWidget_tabBarClicked(int index)
{
    us->setValue("mainwindow/tabIndex", index);
    if (index > 2)
        appendListItemButton->hide();
    else
        appendListItemButton->show();
    QTimer::singleShot(0, [=]{
        adjustPageSize(ui->stackedWidget->currentIndex());
    });
}

void MainWindow::on_SendMsgButton_clicked()
{
//    QString msg = ui->SendMsgEdit->text();
//    msg = processDanmakuVariants(msg, LiveDanmaku());
//    sendAutoMsg(msg);
}

void MainWindow::on_AIReplyCheck_stateChanged(int)
{
    bool reply = ui->AIReplyCheck->isChecked();
    us->setValue("danmaku/aiReply", reply);
    if (danmakuWindow)
        danmakuWindow->setAIReply(reply);

    ui->AIReplyMsgCheck->setEnabled(reply);
}

void MainWindow::on_testDanmakuEdit_returnPressed()
{
    on_testDanmakuButton_clicked();

    ui->testDanmakuEdit->setText("");
}

void MainWindow::on_SendMsgEdit_returnPressed()
{
    QString msg = ui->SendMsgEdit->text();
    msg = processDanmakuVariants(msg, LiveDanmaku());
    sendAutoMsg(msg, LiveDanmaku());
    ui->SendMsgEdit->clear();
}

TaskWidget* MainWindow::addTimerTask(bool enable, int second, QString text, int index)
{
    TaskWidget* tw = new TaskWidget(this);
    QListWidgetItem* item;

    if (index == -1)
    {
        item = new QListWidgetItem(ui->taskListWidget);
        ui->taskListWidget->addItem(item);
        ui->taskListWidget->setItemWidget(item, tw);
        // ui->taskListWidget->setCurrentRow(ui->taskListWidget->count()-1);
    }
    else
    {
        item = new QListWidgetItem();
        ui->taskListWidget->insertItem(index, item);
        ui->taskListWidget->setItemWidget(item, tw);
        ui->taskListWidget->setCurrentRow(index);
    }

    // 连接信号
    connectTimerTaskEvent(tw, item);

    remoteControl = us->value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    tw->check->setChecked(enable);
    tw->spin->setValue(second);
    tw->slotSpinChanged(second);
    tw->edit->setPlainText(text);
    tw->autoResizeEdit();
    tw->adjustSize();

    return tw;
}

TaskWidget *MainWindow::addTimerTask(const MyJson &json)
{
    auto item = addTimerTask(false, 1800, "");
    item->fromJson(json);
    return item;
}

void MainWindow::connectTimerTaskEvent(TaskWidget *tw, QListWidgetItem *item)
{
    connect(tw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = tw->check->isChecked();
        int row = ui->taskListWidget->row(item);
        us->setValue("task/r"+QString::number(row)+"Enable", enable);
    });

    connect(tw, &TaskWidget::spinChanged, this, [=](int val){
        int row = ui->taskListWidget->row(item);
        us->setValue("task/r"+QString::number(row)+"Interval", val);
    });

    connect(tw->edit, &ConditionEditor::textChanged, this, [=]{
        item->setSizeHint(tw->sizeHint());

        QString content = tw->edit->toPlainText();
        int row = ui->taskListWidget->row(item);
        us->setValue("task/r"+QString::number(row)+"Msg", content);
    });

    connect(tw, &TaskWidget::signalSendMsgs, this, [=](QString sl, bool manual){
        if (debugPrint)
            localNotify("[定时任务:" + snum(ui->taskListWidget->row(item)) + "]");
        if (!manual && !shallAutoMsg(sl, manual)) // 没有开播，不进行定时任务
        {
            qInfo() << "未开播，不做回复(timer)" << sl;
            if (debugPrint)
                localNotify("[未开播，不做回复]");
            return ;
        }

        if (sendVariantMsg(sl, LiveDanmaku(), TASK_CD_CN, manual))
        {
        }
        else if (debugPrint)
        {
            localNotify("[没有可发送的定时弹幕]");
        }
    });

    connect(tw, &TaskWidget::signalResized, tw, [=]{
        item->setSizeHint(tw->size());
    });

    connect(tw, &TaskWidget::signalInsertCodeSnippets, this, [=](const QJsonDocument& doc) {
        if (tw->isEmpty())
        {
            int row = ui->taskListWidget->row(item);
            ui->taskListWidget->removeItemWidget(item);
            ui->taskListWidget->takeItem(row);
            tw->deleteLater();
            saveTaskList();
        }
        addCodeSnippets(doc);
    });
}

void MainWindow::saveTaskList()
{
    us->setValue("task/count", ui->taskListWidget->count());
    for (int row = 0; row < ui->taskListWidget->count(); row++)
    {
        auto widget = ui->taskListWidget->itemWidget(ui->taskListWidget->item(row));
        auto tw = static_cast<TaskWidget*>(widget);
        us->setValue("task/r"+QString::number(row)+"Enable", tw->check->isChecked());
        us->setValue("task/r"+QString::number(row)+"Interval", tw->spin->value());
        us->setValue("task/r"+QString::number(row)+"Msg", tw->edit->toPlainText());
    }
}

void MainWindow::restoreTaskList()
{
    int count = us->value("task/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = us->value("task/r"+QString::number(row)+"Enable", false).toBool();
        int interval = us->value("task/r"+QString::number(row)+"Interval", 1800).toInt();
        QString msg = us->value("task/r"+QString::number(row)+"Msg", "").toString();
        addTimerTask(enable, interval, msg);
    }
}

ReplyWidget* MainWindow::addAutoReply(bool enable, QString key, QString reply, int index)
{
    ReplyWidget* rw = new ReplyWidget(this);
    QListWidgetItem* item;

    if (index == -1)
    {
        item = new QListWidgetItem(ui->replyListWidget);
        ui->replyListWidget->addItem(item);
        ui->replyListWidget->setItemWidget(item, rw);
        // ui->replyListWidget->setCurrentRow(ui->replyListWidget->count()-1);
    }
    else
    {
        item = new QListWidgetItem();
        ui->replyListWidget->insertItem(index, item);
        ui->replyListWidget->setItemWidget(item, rw);
        ui->replyListWidget->setCurrentRow(index);
    }

    // 连接信号
    connectAutoReplyEvent(rw, item);

    remoteControl = us->value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    rw->check->setChecked(enable);
    rw->keyEdit->setText(key);
    rw->replyEdit->setPlainText(reply);
    rw->autoResizeEdit();
    rw->adjustSize();
    item->setSizeHint(rw->sizeHint());

    return rw;
}

ReplyWidget *MainWindow::addAutoReply(const MyJson &json)
{
    auto item = addAutoReply(false, "", "");
    item->fromJson(json);
    return item;
}

void MainWindow::connectAutoReplyEvent(ReplyWidget *rw, QListWidgetItem *item)
{
    connect(rw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = rw->check->isChecked();
        int row = ui->replyListWidget->row(item);
        us->setValue("reply/r"+QString::number(row)+"Enable", enable);
    });

    connect(rw->keyEdit, &QLineEdit::textChanged, this, [=](const QString& text){
        int row = ui->replyListWidget->row(item);
        us->setValue("reply/r"+QString::number(row)+"Key", text);
    });

    connect(rw->replyEdit, &ConditionEditor::textChanged, this, [=]{
        item->setSizeHint(rw->sizeHint());

        QString content = rw->replyEdit->toPlainText();
        int row = ui->replyListWidget->row(item);
        us->setValue("reply/r"+QString::number(row)+"Reply", content);
    });

    connect(this, SIGNAL(signalNewDanmaku(const LiveDanmaku&)), rw, SLOT(slotNewDanmaku(const LiveDanmaku&)));

    connect(rw, &ReplyWidget::signalReplyMsgs, this, [=](QString sl, LiveDanmaku danmaku, bool manual){
#ifndef ZUOQI_ENTRANCE
        if (!hasPermission())
            return ;
#endif
        if (isFilterRejected("FILTER_AUTO_REPLY", danmaku))
            return ;
        if ((!manual && !shallAutoMsg(sl, manual)) || danmaku.isPkLink()) // 没有开播，不进行自动回复
        {
            if (!danmaku.isPkLink())
                qInfo() << "未开播，不做回复(reply)" << sl;
            if (debugPrint)
                localNotify("[未开播，不做回复]");
            return ;
        }

        if (sendVariantMsg(sl, danmaku, REPLY_CD_CN, manual, true))
        {
        }
        else if (debugPrint)
        {
            localNotify("[没有可发送的回复弹幕]");
        }
    });

    connect(rw, &ReplyWidget::signalResized, rw, [=]{
        item->setSizeHint(rw->size());
    });

    connect(rw, &ReplyWidget::signalInsertCodeSnippets, this, [=](const QJsonDocument& doc) {
        if (rw->isEmpty())
        {
            int row = ui->replyListWidget->row(item);
            ui->replyListWidget->removeItemWidget(item);
            ui->replyListWidget->takeItem(row);
            rw->deleteLater();
            saveReplyList();
        }
        addCodeSnippets(doc);
    });
}

void MainWindow::saveReplyList()
{
    us->setValue("reply/count", ui->replyListWidget->count());
    for (int row = 0; row < ui->replyListWidget->count(); row++)
    {
        auto widget = ui->replyListWidget->itemWidget(ui->replyListWidget->item(row));
        auto tw = static_cast<ReplyWidget*>(widget);
        us->setValue("reply/r"+QString::number(row)+"Enable", tw->check->isChecked());
        us->setValue("reply/r"+QString::number(row)+"Key", tw->keyEdit->text());
        us->setValue("reply/r"+QString::number(row)+"Reply", tw->replyEdit->toPlainText());
    }
}

void MainWindow::restoreReplyList()
{
    int count = us->value("reply/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = us->value("reply/r"+QString::number(row)+"Enable", false).toBool();
        QString key = us->value("reply/r"+QString::number(row)+"Key").toString();
        QString reply = us->value("reply/r"+QString::number(row)+"Reply").toString();
        addAutoReply(enable, key, reply);
    }
}

void MainWindow::addListItemOnCurrentPage()
{
    auto w = ui->tabWidget->currentWidget();
    if (w == ui->tabTimer)
    {
        addTimerTask(false, 1800, "");
        saveTaskList();
        auto widget = ui->taskListWidget->itemWidget(ui->taskListWidget->item(ui->taskListWidget->count()-1));
        auto tw = static_cast<TaskWidget*>(widget);
        QTimer::singleShot(0, [=]{
            ui->taskListWidget->scrollToBottom();
        });
        tw->edit->setFocus();
    }
    else if (w == ui->tabReply)
    {
        addAutoReply(false, "", "");
        saveReplyList();
        auto widget = ui->replyListWidget->itemWidget(ui->replyListWidget->item(ui->replyListWidget->count()-1));
        auto rw = static_cast<ReplyWidget*>(widget);
        QTimer::singleShot(0, [=]{
            ui->replyListWidget->scrollToBottom();
        });
        rw->keyEdit->setFocus();
    }
    else if (w == ui->tabEvent)
    {
        addEventAction(false, "", "");
        saveEventList();
        auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(ui->eventListWidget->count()-1));
        auto ew = static_cast<EventWidget*>(widget);
        QTimer::singleShot(0, [=]{
            ui->eventListWidget->scrollToBottom();
        });
        ew->eventEdit->setFocus();
    }
}

EventWidget* MainWindow::addEventAction(bool enable, QString cmd, QString action, int index)
{
    EventWidget* rw = new EventWidget(this);
    QListWidgetItem* item;

    if (index == -1)
    {
        item = new QListWidgetItem(ui->eventListWidget);
        ui->eventListWidget->addItem(item);
        ui->eventListWidget->setItemWidget(item, rw);
        // ui->eventListWidget->setCurrentRow(ui->eventListWidget->count()-1);
    }
    else
    {
        item = new QListWidgetItem();
        ui->eventListWidget->insertItem(index, item);
        ui->eventListWidget->setItemWidget(item, rw);
        ui->eventListWidget->setCurrentRow(index);
    }

    // 连接信号
    connectEventActionEvent(rw, item);

    remoteControl = us->value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    rw->check->setChecked(enable);
    rw->eventEdit->setText(cmd);
    rw->actionEdit->setPlainText(action);
    rw->autoResizeEdit();
    rw->adjustSize();
    item->setSizeHint(rw->sizeHint());

    return rw;
}

EventWidget* MainWindow::addEventAction(const MyJson &json)
{
    auto item = addEventAction(false, "", "");
    item->fromJson(json);
    return item;
}

void MainWindow::connectEventActionEvent(EventWidget *rw, QListWidgetItem *item)
{
    connect(rw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = rw->check->isChecked();
        int row = ui->eventListWidget->row(item);
        us->setValue("event/r"+QString::number(row)+"Enable", enable);
    });

    connect(rw->eventEdit, &QLineEdit::textChanged, this, [=](const QString& text){
        int row = ui->eventListWidget->row(item);
        us->setValue("event/r"+QString::number(row)+"Cmd", text);
    });

    connect(rw->actionEdit, &ConditionEditor::textChanged, this, [=]{
        item->setSizeHint(rw->sizeHint());

        QString content = rw->actionEdit->toPlainText();
        int row = ui->eventListWidget->row(item);
        us->setValue("event/r"+QString::number(row)+"Action", content);

        /* // 处理特殊操作，比如过滤器
        QString event = rw->title();
        if (!event.isEmpty())
        {
            setFilter(event, content);
        } */
    });

    connect(this, SIGNAL(signalCmdEvent(QString, LiveDanmaku)), rw, SLOT(triggerCmdEvent(QString,LiveDanmaku)));

    connect(rw, &EventWidget::signalEventMsgs, this, [=](QString sl, LiveDanmaku danmaku, bool manual){
#ifndef ZUOQI_ENTRANCE
        if (!hasPermission())
            return ;
#endif
        if (!manual && !shallAutoMsg(sl, manual)) // 没有开播，不进行自动回复
        {
            qInfo() << "未开播，不做操作(event)" << sl;
            if (debugPrint)
                localNotify("[未开播，不做操作]");
            return ;
        }
        if (!isLiving() && !manual)
            manual = true;

        if (sendVariantMsg(sl, danmaku, EVENT_CD_CN, manual))
        {
        }
        else if (debugPrint)
        {
            localNotify("[没有可发送的事件弹幕]");
        }
    });

    connect(rw, &EventWidget::signalResized, rw, [=]{
        item->setSizeHint(rw->size());
    });

    connect(rw, &EventWidget::signalInsertCodeSnippets, this, [=](const QJsonDocument& doc) {
        if (rw->isEmpty())
        {
            int row = ui->eventListWidget->row(item);
            ui->eventListWidget->removeItemWidget(item);
            ui->eventListWidget->takeItem(row);
            rw->deleteLater();
            saveEventList();
        }
        addCodeSnippets(doc);
    });
}

void MainWindow::saveEventList()
{
    us->setValue("event/count", ui->eventListWidget->count());
    for (int row = 0; row < ui->eventListWidget->count(); row++)
    {
        auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(row));
        auto tw = static_cast<EventWidget*>(widget);
        us->setValue("event/r"+QString::number(row)+"Enable", tw->check->isChecked());
        us->setValue("event/r"+QString::number(row)+"Cmd", tw->eventEdit->text());
        us->setValue("event/r"+QString::number(row)+"Action", tw->actionEdit->toPlainText());
    }
}

void MainWindow::restoreEventList()
{
    int count = us->value("event/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = us->value("event/r"+QString::number(row)+"Enable", false).toBool();
        QString key = us->value("event/r"+QString::number(row)+"Cmd").toString();
        QString event = us->value("event/r"+QString::number(row)+"Action").toString();
        addEventAction(enable, key, event);
    }
}

bool MainWindow::hasEvent(const QString &cmd) const
{
    for (int row = 0; row < ui->eventListWidget->count(); row++)
    {
        auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(row));
        auto tw = static_cast<EventWidget*>(widget);
        if (tw->eventEdit->text() == cmd && tw->check->isChecked())
            return true;
    }
    return false;
}

void MainWindow::addCodeSnippets(const QJsonDocument &doc)
{
    bool scrolled[3] = {};

    auto pasteFromJson = [&](QJsonObject json) {
        QString anchor_key = json.value("anchor_key").toString();
        ListItemInterface* item = nullptr;
        if (anchor_key == CODE_TIMER_TASK_KEY)
        {
            item = addTimerTask(false, 1800, "");
            ui->tabWidget->setCurrentWidget(ui->tabTimer);
            if (!scrolled[0])
            {
                scrolled[0] = true;
                QTimer::singleShot(0, [=]{
                    ui->taskListWidget->scrollToBottom();
                });
            }
            us->setValue("task/count", ui->taskListWidget->count());
        }
        else if (anchor_key == CODE_AUTO_REPLY_KEY)
        {
            item = addAutoReply(false, "","");
            ui->tabWidget->setCurrentWidget(ui->tabReply);
            if (!scrolled[1])
            {
                scrolled[1] = true;
                QTimer::singleShot(0, [=]{
                    ui->replyListWidget->scrollToBottom();
                });
            }
            us->setValue("reply/count", ui->replyListWidget->count());
        }
        else if (anchor_key == CODE_EVENT_ACTION_KEY)
        {
            item = addEventAction(false, "", "");
            ui->tabWidget->setCurrentWidget(ui->tabEvent);
            if (!scrolled[2])
            {
                scrolled[2] = true;
                QTimer::singleShot(0, [=]{
                    ui->eventListWidget->scrollToBottom();
                });
            }
            us->setValue("event/count", ui->eventListWidget->count());
        }
        else
        {
            qWarning() << "未知格式：" << json;
            return ;
        }

        item->fromJson(json);
        item->autoResizeEdit();

        // 检查重复
        if (anchor_key == CODE_AUTO_REPLY_KEY)
        {
            for (int row = 0; row < ui->replyListWidget->count() - 1; row++)
            {
                auto rowItem = ui->replyListWidget->item(row);
                auto widget = ui->replyListWidget->itemWidget(rowItem);
                if (!widget)
                    continue;
                QSize size(ui->replyListWidget->contentsRect().width() - ui->replyListWidget->verticalScrollBar()->width(), widget->height());
                auto replyWidget = static_cast<ReplyWidget*>(widget);

                auto rw = static_cast<ReplyWidget*>(item);
                if (rw->keyEdit->text() == replyWidget->keyEdit->text()
                        && rw->replyEdit->toPlainText() == replyWidget->replyEdit->toPlainText())
                {
                    rw->check->setChecked(false);
                    rw->keyEdit->setText(rw->keyEdit->text() + "_重复");
                    break;
                }
            }
        }
        else if (anchor_key == CODE_EVENT_ACTION_KEY)
        {
            for (int row = 0; row < ui->eventListWidget->count() - 1; row++)
            {
                auto rowItem = ui->eventListWidget->item(row);
                auto widget = ui->eventListWidget->itemWidget(rowItem);
                if (!widget)
                    continue;
                QSize size(ui->eventListWidget->contentsRect().width() - ui->eventListWidget->verticalScrollBar()->width(), widget->height());
                auto eventWidget = static_cast<EventWidget*>(widget);

                auto rw = static_cast<EventWidget*>(item);
                if (rw->eventEdit->text() == eventWidget->eventEdit->text()
                        && rw->actionEdit->toPlainText() == eventWidget->actionEdit->toPlainText())
                {
                    rw->check->setChecked(false);
                    rw->eventEdit->setText(rw->eventEdit->text() + "_重复");
                    break;
                }
            }
        }
    };

    if (doc.isObject())
    {
        pasteFromJson(doc.object());
    }
    else if (doc.isArray())
    {
        QJsonArray array = doc.array();
        foreach (QJsonValue val, array)
        {
            pasteFromJson(val.toObject());
        }
    }
    adjustPageSize(PAGE_EXTENSION);
}

void MainWindow::autoSetCookie(const QString &s)
{
    us->setValue("danmaku/browserCookie", ac->browserCookie = s);
    if (ac->browserCookie.isEmpty())
        return ;

    ac->userCookies = getCookies();
    getCookieAccount();

    // 自动设置弹幕格式
    int posl = ac->browserCookie.indexOf("bili_jct=") + 9;
    if (posl == -1)
        return ;
    int posr = ac->browserCookie.indexOf(";", posl);
    if (posr == -1) posr = ac->browserCookie.length();
    ac->csrf_token = ac->browserCookie.mid(posl, posr - posl);
    qInfo() << "检测到csrf_token:" << ac->csrf_token;

    if (ac->browserData.isEmpty())
        ac->browserData = "color=4546550&fontsize=25&mode=4&msg=&rnd=1605156247&roomid=&bubble=5&csrf_token=&csrf=";
    ac->browserData.replace(QRegularExpression("csrf_token=[^&]*"), "csrf_token=" + ac->csrf_token);
    ac->browserData.replace(QRegularExpression("csrf=[^&]*"), "csrf=" + ac->csrf_token);
    us->setValue("danmaku/browserData", ac->browserData);
    qInfo() << "设置弹幕格式：" << ac->browserData;
}

QVariant MainWindow::getCookies() const
{
    QList<QNetworkCookie> cookies;

    // 设置cookie
    QString cookieText = ac->browserCookie;
    QStringList sl = cookieText.split(";");
    foreach (auto s, sl)
    {
        s = s.trimmed();
        int pos = s.indexOf("=");
        QString key = s.left(pos);
        QString val = s.right(s.length() - pos - 1);
        cookies.push_back(QNetworkCookie(key.toUtf8(), val.toUtf8()));
    }

    // 请求头里面加入cookies
    QVariant var;
    var.setValue(cookies);
    return var;
}

/**
 * 获取用户信息
 */
void MainWindow::getCookieAccount()
{
    if (ac->browserCookie.isEmpty())
        return ;
    gettingUser = true;
    get("http://api.bilibili.com/x/member/web/account", [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            showError("登录返回不为0", json.value("message").toString());
            gettingUser = false;
            if (!gettingRoom)
                triggerCmdEvent("LOGIN_FINISHED", LiveDanmaku());
            updatePermission();
            return ;
        }

        // 获取用户信息
        QJsonObject dataObj = json.value("data").toObject();
        ac->cookieUid = snum(static_cast<qint64>(dataObj.value("mid").toDouble()));
        ac->cookieUname = dataObj.value("uname").toString();
        qInfo() << "当前账号：" << ac->cookieUid << ac->cookieUname;
        ui->robotNameButton->setText(ac->cookieUname);
        ui->robotNameButton->adjustMinimumSize();
        ui->robotInfoWidget->setMinimumWidth(ui->robotNameButton->width());
#ifdef ZUOQI_ENTRANCE
        fakeEntrance->setRobotName(ac->cookieUname);
#endif

        getRobotInfo();
        gettingUser = false;
        if (!gettingRoom)
            triggerCmdEvent("LOGIN_FINISHED", LiveDanmaku());
        updatePermission();
        getPositiveVote();
    });
}

QString MainWindow::getDomainPort() const
{
    QString domain = ui->domainEdit->text().trimmed();
    if (domain.isEmpty())
        domain = "http://localhost";
    if (!domain.contains("://"))
        domain = "http://" + domain;
    QString url = domain + ":" + snum(ui->serverPortSpin->value());
    return url;
}

void MainWindow::getRobotInfo()
{
    QString url = "http://api.bilibili.com/x/space/acc/info?mid=" + ac->cookieUid;
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("获取账号信息返回结果不为0：") << json.value("message").toString();
            return ;
        }
        QJsonObject data = json.value("data").toObject();

        // 开始下载头像
        QString face = data.value("face").toString();
        get(face, [=](QNetworkReply* reply){
            QByteArray jpegData = reply->readAll();
            QPixmap pixmap;
            pixmap.loadFromData(jpegData);
            if (pixmap.isNull())
            {
                showError("获取账号头像出错");
                return ;
            }

            // 设置成圆角
            int side = qMin(pixmap.width(), pixmap.height());
            QPixmap p(side, side);
            p.fill(Qt::transparent);
            QPainter painter(&p);
            painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            QPainterPath path;
            path.addEllipse(0, 0, side, side);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, side, side, pixmap);

            // 设置到Robot头像
            QPixmap f = p.scaled(ui->robotHeaderLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            ui->robotHeaderLabel->setPixmap(f);
        });
    });

    // 检查是否需要用户信息
    if (!ui->adjustDanmakuLongestCheck->isChecked())
        return ;

    // 获取弹幕字数
    url = "https://api.vc.bilibili.com/user_ex/v1/user/detail?uid=" + ac->cookieUid + "&user[]=role&user[]=level&room[]=live_status&room[]=room_link&feed[]=fans_count&feed[]=feed_count&feed[]=is_followed&feed[]=is_following&platform=pc";
    get(url, [=](MyJson json) {
        /*{
            "code": 0,
            "msg": "success",
            "message": "success",
            "data": {
                "user": {
                    "role": 2,
                    "user_level": 19,
                    "master_level": 5,
                    "next_master_level": 6,
                    "need_master_score": 704,
                    "master_rank": 100010,
                    "verify": 0
                },
                "feed": {
                    "fans_count": 7084,
                    "feed_count": 41,
                    "is_followed": 0,
                    "is_following": 0
                },
                "room": {
                    "live_status": 0,
                    "room_id": 11584296,
                    "short_room_id": 0,
                    "title": "用户20285041的直播间",
                    "cover": "",
                    "keyframe": "",
                    "online": 0,
                    "room_link": "http://live.bilibili.com/11584296?src=draw"
                },
                "uid": "20285041"
            }
        }*/
        MyJson data = json.data();
        ac->cookieULevel = data.o("user").i("user_level");
        if (ui->adjustDanmakuLongestCheck->isChecked())
            adjustDanmakuLongest();
    });
}

/**
 * 获取用户在当前直播间的信息
 * 会触发进入直播间，就不能偷看了……
 */
void MainWindow::getRoomUserInfo()
{
    /*{
        "code": 0,
        "message": "0",
        "ttl": 1,
        "data": {
            "user_level": {
                "level": 13,
                "next_level": 14,
                "color": 6406234,
                "level_rank": ">50000"
            },
            "vip": {
                "vip": 0,
                "vip_time": "0000-00-00 00:00:00",
                "svip": 0,
                "svip_time": "0000-00-00 00:00:00"
            },
            "title": {
                "title": ""
            },
            "badge": {
                "is_room_admin": false
            },
            "privilege": {
                "target_id": 0,
                "privilege_type": 0,
                "privilege_uname_color": "",
                "buy_guard_notice": null,
                "sub_level": 0,
                "notice_status": 1,
                "expired_time": "",
                "auto_renew": 0,
                "renew_remind": null
            },
            "info": {
                "uid": 20285041,
                "uname": "懒一夕智能科技",
                "uface": "http://i1.hdslb.com/bfs/face/97ae8f0f0e09fbc22fa680c4f5ed93f92678c9eb.jpg",
                "main_rank": 10000,
                "bili_vip": 2,
                "mobile_verify": 1,
                "identification": 1
            },
            "property": {
                "uname_color": "",
                "bubble": 0,
                "danmu": {
                    "mode": 1,
                    "color": 16777215,
                    "length": 20,
                    "room_id": 13328782
                },
                "bubble_color": ""
            },
            "recharge": {
                "status": 0,
                "type": 1,
                "value": "",
                "color": "fb7299",
                "config_id": 0
            },
            "relation": {
                "is_followed": false,
                "is_fans": false,
                "is_in_fansclub": false
            },
            "wallet": {
                "gold": 6100,
                "silver": 3294
            },
            "medal": {
                "cnt": 16,
                "is_weared": false,
                "curr_weared": null,
                "up_medal": {
                    "uid": 358629230,
                    "medal_name": "石乐志",
                    "medal_color": 6067854,
                    "level": 1
                }
            },
            "extra_config": {
                "show_bag": false,
                "show_vip_broadcast": true
            },
            "mailbox": {
                "switch_status": 0,
                "red_notice": 0
            },
            "user_reward": {
                "entry_effect": {
                    "id": 0,
                    "privilege_type": 0,
                    "priority": 0,
                    "web_basemap_url": "",
                    "web_effective_time": 0,
                    "web_effect_close": 0,
                    "web_close_time": 0,
                    "copy_writing": "",
                    "copy_color": "",
                    "highlight_color": "",
                    "mock_effect": 0,
                    "business": 0,
                    "face": "",
                    "basemap_url": "",
                    "show_avatar": 0,
                    "effective_time": 0
                },
                "welcome": {
                    "allow_mock": 1
                }
            },
            "shield_info": {
                "shield_user_list": [],
                "keyword_list": [],
                "shield_rules": {
                    "rank": 0,
                    "verify": 0,
                    "level": 0
                }
            },
            "super_chat_message": {
                "list": []
            },
            "lpl_info": {
                "lpl": 0
            },
            "cd": {
                "guide_free_medal_cost": 0,
                "guide_light_medal": 0,
                "guide_follow": 1,
                "guard_compensate": 0,
                "interact_toasts": []
            }
        }
    }*/

    if (ac->browserCookie.isEmpty())
        return ;
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByUser?room_id=" + ac->roomId;
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("获取房间本用户信息返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户在这个房间信息
        QJsonObject info = json.value("data").toObject().value("info").toObject();
        ac->cookieUid = snum(static_cast<qint64>(info.value("uid").toDouble()));
        ac->cookieUname = info.value("uname").toString();
        QString uface = info.value("uface").toString(); // 头像地址
        qInfo() << "当前cookie用户：" << ac->cookieUid << ac->cookieUname;
    });
}

template<class T>
void MainWindow::showListMenu(QListWidget *listWidget, QString listKey, VoidFunc saveFunc)
{
    QListWidgetItem* item = listWidget->currentItem();
    int row = listWidget->currentRow();

    QString clipText = QApplication::clipboard()->text();
    bool canPaste = false, canContinueCopy = false;
    MyJson clipJson;
    QJsonArray clipArray;
    if (!clipText.isEmpty())
    {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(clipText.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
        {
            if (doc.isObject())
            {
                clipJson = doc.object();
                JS(clipJson, anchor_key);
                canPaste = (anchor_key == listKey);
                qInfo() << anchor_key << listKey;
            }
            else if (doc.isArray())
            {
                clipArray = doc.array();
            }
            canContinueCopy = true;
        }
    }

    auto moveToRow = [=](int newRow){
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        MyJson json = tw->toJson();
        listWidget->removeItemWidget(item);
        listWidget->takeItem(row);

        delete item;
        tw->deleteLater();

        auto newTw = new T(listWidget);
        newTw->fromJson(json);
        QListWidgetItem* newItem = new QListWidgetItem;

        listWidget->insertItem(newRow, newItem);
        listWidget->setItemWidget(newItem, newTw);
        newTw->autoResizeEdit();
        newItem->setSizeHint(newTw->sizeHint());

        /* // 这个最简单的方式行不通，不知道为什么widget都会被删掉
        auto item = listWidget->takeItem(row);
        listWidget->insertItem(newRow, item); */

        if (listKey == CODE_TIMER_TASK_KEY)
            connectTimerTaskEvent((TaskWidget*)newTw, newItem);
        else if (listKey == CODE_AUTO_REPLY_KEY)
            connectAutoReplyEvent((ReplyWidget*)newTw, newItem);
        else if (listKey == CODE_EVENT_ACTION_KEY)
            connectEventActionEvent((EventWidget*)newTw, newItem);
        (this->*saveFunc)();
        listWidget->setCurrentRow(newRow);
    };

    auto menu = new FacileMenu(this);
    menu->addAction("插入 (&a)", [=]{
        if (listKey == CODE_TIMER_TASK_KEY)
            addTimerTask(false, 1800, "", row);
        else if (listKey == CODE_AUTO_REPLY_KEY)
            addAutoReply(false, "", "", row);
        else if (listKey == CODE_EVENT_ACTION_KEY)
            addEventAction(false, "", "", row);
        (this->*saveFunc)();
    })->disable(!item);
    menu->addAction("上移 (&w)", [=]{
        moveToRow(row - 1);
    })->disable(!item || row <= 0);
    menu->addAction("下移 (&s)", [=]{
        moveToRow(row + 1);
    })->disable(!item || row >= listWidget->count()-1);
    menu->split()->addAction("复制片段 (&c)", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        QApplication::clipboard()->setText(tw->toJson().toBa());
    })->disable(!item);
    menu->addAction("继续复制 (&n)", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        MyJson twJson = tw->toJson();
        if (!clipArray.isEmpty()) // 已经是JSON数组了，继续复制
        {
            QJsonArray array = clipArray;
            array.append(twJson);
            QApplication::clipboard()->setText(QJsonDocument(array).toJson());
        }
        else // 是JSON对象，需要变成数组
        {
            QJsonArray array;
            array.append(clipJson);
            array.append(twJson);
            QApplication::clipboard()->setText(QJsonDocument(array).toJson());
        }
    })->disable(!item)->hide(!canContinueCopy)->tooltip("多次复制代码片段，组合成一串json格式数组\n可用来导入导出");
    menu->addAction("粘贴片段 (&v)", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        tw->fromJson(clipJson);

        (this->*saveFunc)();
    })->disable(!canPaste)->tooltip("复制代码片段后可粘贴至当前位置\n可以快速导入他人的片段");
    menu->split()->addAction("删除 (&d)", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);

        // 特殊操作
        /* if (listKey == CODE_EVENT_ACTION_KEY)
        {
            setFilter(tw->title(), "");
        } */

        listWidget->removeItemWidget(item);
        listWidget->takeItem(listWidget->currentRow());

        (this->*saveFunc)();
        tw->deleteLater();
    })->disable(!item);
    menu->exec();
}

void MainWindow::on_taskListWidget_customContextMenuRequested(const QPoint &)
{
    showListMenu<TaskWidget>(ui->taskListWidget, CODE_TIMER_TASK_KEY, &MainWindow::saveTaskList);
}

void MainWindow::on_replyListWidget_customContextMenuRequested(const QPoint &)
{
    showListMenu<ReplyWidget>(ui->replyListWidget, CODE_AUTO_REPLY_KEY, &MainWindow::saveReplyList);
}

void MainWindow::on_eventListWidget_customContextMenuRequested(const QPoint &)
{
    showListMenu<EventWidget>(ui->eventListWidget, CODE_EVENT_ACTION_KEY, &MainWindow::saveEventList);
}

void MainWindow::slotDiange(const LiveDanmaku &danmaku)
{
    if (danmaku.getMsgType() != MSG_DANMAKU || danmaku.isPkLink() || danmaku.isNoReply())
        return ;
    QRegularExpression re(diangeFormatString);
    QRegularExpressionMatch match;
    if (danmaku.getText().indexOf(re, 0, &match) == -1) // 不是点歌文本
        return ;

    if (match.capturedTexts().size() < 2)
    {
        showError("无法获取点歌内容，请检测点歌格式");
        return ;
    }
    if (ui->diangeNeedMedalCheck->isChecked())
    {
        if (danmaku.getAnchorRoomid() != ac->roomId) // 不是对应的粉丝牌
        {
            qWarning() << "点歌未戴粉丝勋章：" << danmaku.getNickname() << danmaku.getAnchorRoomid() << "!=" << ac->roomId;
            localNotify("点歌未戴粉丝勋章");
            triggerCmdEvent("ORDER_SONG_NO_MEDAL", danmaku, true);
            return ;
        }
    }

    // 记录到历史（先不复制）
    QString text = match.capturedTexts().at(1);
    text = text.trimmed();
    qInfo() << s8("检测到点歌：") << text;
    diangeHistory.append(Diange{danmaku.getNickname(), danmaku.getUid(), text, danmaku.getTimeline()});
    ui->diangeHistoryListWidget->insertItem(0, text + " - " + danmaku.getNickname());

    // 总开关，是否进行复制/播放操作
    if (!diangeAutoCopy)
        return ;

    // 判断关键词
    QString lower = text.toLower();
    foreach (QString s, orderSongBlackList)
    {
        if (lower.contains(s)) // 有违禁词
        {
            MyJson json;
            json.insert("key", s);
            qInfo() << "阻止点歌，关键词：" << s;
            LiveDanmaku dmk = danmaku;
            triggerCmdEvent("ORDER_SONG_BLOCKED", dmk.with(json), true);
            return ;
        }
    }

    // 判断过滤器
    if (isFilterRejected("FILTER_MUSIC_ORDER", danmaku))
        return ;

    // 成功点歌
    if (saveToSqlite)
    {
        LiveDanmaku md = danmaku;
        md.setText(text);
        sqlService.insertMusic(md);
    }

    if (musicWindow && !musicWindow->isHidden()) // 自动播放
    {
        int count = 0;
        if (ui->diangeShuaCheck->isChecked() && (count = musicWindow->userOrderCount(danmaku.getNickname())) >= ui->orderSongShuaSpin->value()) // 已经点了
        {
            localNotify("已阻止频繁点歌：" + snum(count));
            LiveDanmaku dmk = danmaku;
            dmk.setNumber(count);
            triggerCmdEvent("ORDER_SONG_FREQUENCY", dmk, true);
        }
        else
        {
            musicWindow->slotSearchAndAutoAppend(text, danmaku.getNickname());
        }
    }
    else // 复制到剪贴板
    {
        QClipboard* clip = QApplication::clipboard();
        clip->setText(text);
        triggerCmdEvent("ORDER_SONG_COPY", danmaku, true);

        addNoReplyDanmakuText(danmaku.getText()); // 点歌回复
        QTimer::singleShot(10, [=]{
            appendNewLiveDanmaku(LiveDanmaku(danmaku.getNickname(), danmaku.getUid(), text, danmaku.getTimeline()));
        });
    }
}

void MainWindow::slotSocketError(QAbstractSocket::SocketError error)
{
    showError("socket", socket->errorString());
}

void MainWindow::initWS()
{
    socket = new QWebSocket();

    connect(socket, &QWebSocket::connected, this, [=]{
        SOCKET_DEB << "socket connected";
        ui->connectStateLabel->setText("状态：已连接");

        // 5秒内发送认证包
        sendVeriPacket(socket, ac->roomId, ac->cookieToken);

        // 定时发送心跳包
        heartTimer->start();
        minuteTimer->start();
    });

    connect(socket, &QWebSocket::disconnected, this, [=]{
        // 正在直播的时候突然断开了
        if (isLiving())
        {
            qWarning() << "正在直播的时候突然断开，" << (reconnectWSDuration/1000) << "秒后重连..." << "    " << QDateTime::currentDateTime().toString("HH:mm:ss");
            localNotify("[连接断开，重连...]");
            ac->liveStatus = false;
            // 尝试5秒钟后重连
            // TODO: 每次重连间隔翻倍
            connectServerTimer->setInterval(reconnectWSDuration);
            reconnectWSDuration *= 2;
            if (reconnectWSDuration > INTERVAL_RECONNECT_WS_MAX)
                reconnectWSDuration = INTERVAL_RECONNECT_WS_MAX;
        }

        SOCKET_DEB << "disconnected";
        ui->connectStateLabel->setText("状态：未连接");
        ui->liveStatusButton->setText("");

        heartTimer->stop();
        minuteTimer->stop();

        // 如果不是主动连接的话，这个会断开
        if (!connectServerTimer->isActive())
            connectServerTimer->start();
    });

    connect(socket, &QWebSocket::binaryMessageReceived, this, [=](const QByteArray &message){
//        qInfo() << "binaryMessageReceived" << message;
//        for (int i = 0; i < 100; i++) // 测试内存泄漏
        try {
            slotBinaryMessageReceived(message);

            // 保存到CMDS里
            if (saveRecvCmds && saveCmdsFile)
            {
                QByteArray ba = message;
                ba.replace("\n", "__bmd__n__").replace("\r", "__bmd__r__");
                saveCmdsFile->write(ba);
                saveCmdsFile->write("\n");
            }
        } catch (...) {
            qCritical() << "!!!!!!!error:slotBinaryMessageReceived";
        }

    });

    connect(socket, &QWebSocket::textFrameReceived, this, [=](const QString &frame, bool isLastFrame){
        qInfo() << "textFrameReceived";
    });

    connect(socket, &QWebSocket::textMessageReceived, this, [=](const QString &message){
        qInfo() << "textMessageReceived";
    });

    connect(socket, &QWebSocket::stateChanged, this, [=](QAbstractSocket::SocketState state){
        SOCKET_DEB << "stateChanged" << state;
        QString str = "未知";
        if (state == QAbstractSocket::UnconnectedState)
            str = "未连接";
        else if (state == QAbstractSocket::ConnectingState)
            str = "连接中";
        else if (state == QAbstractSocket::ConnectedState)
            str = "已连接";
        else if (state == QAbstractSocket::BoundState)
            str = "已绑定";
        else if (state == QAbstractSocket::ClosingState)
            str = "断开中";
        else if (state == QAbstractSocket::ListeningState)
            str = "监听中";
        ui->connectStateLabel->setText(str);
    });

    heartTimer = new QTimer(this);
    heartTimer->setInterval(30000);
    connect(heartTimer, &QTimer::timeout, this, [=]{
        if (socket->state() == QAbstractSocket::ConnectedState)
            sendHeartPacket();
        else if (socket->state() == QAbstractSocket::UnconnectedState)
            startConnectRoom();

        // PK Socket
        if (pkSocket && pkSocket->state() == QAbstractSocket::ConnectedState)
        {
            QByteArray ba;
            ba.append("[object Object]");
            ba = BiliApiUtil::makePack(ba, OP_HEARTBEAT);
            pkSocket->sendBinaryMessage(ba);
        }

        // 机器人 Socket
        for (int i = 0; i < robots_sockets.size(); i++)
        {
            QWebSocket* socket = robots_sockets.at(i);
            if (socket->state() == QAbstractSocket::ConnectedState)
            {
                QByteArray ba;
                ba.append("[object Object]");
                ba = BiliApiUtil::makePack(ba, OP_HEARTBEAT);
                socket->sendBinaryMessage(ba);
            }
        }
    });

    xliveHeartBeatTimer = new QTimer(this);
    xliveHeartBeatTimer->setInterval(60000);
    connect(xliveHeartBeatTimer, &QTimer::timeout, this, [=]{
        if (isLiving())
            sendXliveHeartBeatX();
    });
}

void MainWindow::startConnectIdentityCode()
{
    MyJson json;
    json.insert("code", ac->identityCode); // 主播身份码
    json.insert("app_id", (qint64)BILI_APP_ID);
    liveOpenService->post(BILI_API_DOMAIN + "/v2/app/start", json, [=](MyJson json){
        if (json.code() != 0)
        {
            showError("解析身份码出错", snum(json.code()) + " " + json.msg());
            return ;
        }

        auto data = json.data();
        auto anchor = data.o("anchor_info");
        qint64 roomId = anchor.l("room_id");

        // 通过房间号连接
        ui->roomIdEdit->setText(snum(roomId));
        on_roomIdEdit_editingFinished();
    });
}

void MainWindow::startConnectRoom()
{
    if (ac->roomId.isEmpty())
        return ;

    // 判断是身份码还是房间号
    bool isPureRoomId = (ac->roomId.contains(QRegularExpression("^\\d+$")));
    if (!isPureRoomId)
    {
        QTimer::singleShot(100, [=]{
            startConnectIdentityCode();
        });
        return ;
    }

    // 初始化主播数据
    ac->currentFans = 0;
    ac->currentFansClub = 0;
    this->popularVal = 2;

    // 准备房间数据
    if (us->danmakuCounts)
        us->danmakuCounts->deleteLater();
    QDir dir;
    dir.mkdir(rt->dataPath+"danmaku_counts");
    us->danmakuCounts = new QSettings(rt->dataPath+"danmaku_counts/" + ac->roomId + ".ini", QSettings::Format::IniFormat);
    if (ui->calculateDailyDataCheck->isChecked())
        startCalculateDailyData();

    // 开始获取房间信息
    getRoomInfo(true);
    if (ui->enableBlockCheck->isChecked())
        refreshBlockList();
}

void MainWindow::sendXliveHeartBeatE()
{
    if (ac->roomId.isEmpty() || ac->cookieUid.isEmpty() || !isLiving())
        return ;
    if (todayHeartMinite >= ui->heartTimeSpin->value()) // 小心心已经收取满了
    {
        if (xliveHeartBeatTimer)
            xliveHeartBeatTimer->stop();
        return ;
    }

    xliveHeartBeatIndex = 0;

    QString url("https://live-trace.bilibili.com/xlive/data-interface/v1/x25Kn/E");

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "id=" + QString("[%1,%2,%3,%4]").arg(ac->parentAreaId).arg(ac->areaId).arg(xliveHeartBeatIndex).arg(ac->roomId);
    datas << "device=[\"AUTO4115984068636104\",\"f5f08e2f-e4e3-4156-8127-616f79a17e1a\"]";
    datas << "ts=" + snum(QDateTime::currentMSecsSinceEpoch());
    datas << "is_patch=0";
    datas << "heart_beat=[]";
    datas << "ua=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.146 Safari/537.36";
    datas << "csrf_token=" + ac->csrf_token;
    datas << "csrf=" + ac->csrf_token;
    datas << "visit_id=";
    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    post(url, ba, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString message = json.value("message").toString();
            showError("获取小心心失败E", message);
            qCritical() << s8("warning: 发送直播心跳失败E：") << message << datas.join("&");
            /* if (message.contains("sign check failed")) // 没有勋章无法获取？
            {
                ui->acquireHeartCheck->setChecked(false);
                showError("获取小心心", "已临时关闭，可能没有粉丝勋章？");
            } */
            return ;
        }

        /*{
            "code": 0,
            "message": "0",
            "ttl": 1,
            "data": {
                "timestamp": 1612765538,
                "heartbeat_interval": 60,
                "secret_key": "seacasdgyijfhofiuxoannn",
                "secret_rule": [2,5,1,4],
                "patch_status": 2
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        xliveHeartBeatBenchmark = data.value("secret_key").toString();
        xliveHeartBeatEts = qint64(data.value("timestamp").toDouble());
        xliveHeartBeatInterval = data.value("heartbeat_interval").toInt();
        xliveHeartBeatSecretRule = data.value("secret_rule").toArray();

        xliveHeartBeatTimer->start();
        xliveHeartBeatTimer->setInterval(xliveHeartBeatInterval * 1000 - 1000);
        // qDebug() << "直播心跳E：" << xliveHeartBeatBenchmark;
    });
}

void MainWindow::sendXliveHeartBeatX()
{
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // 获取加密的数据
    QJsonObject postData;
    postData.insert("id",  QString("[%1,%2,%3,%4]").arg(ac->parentAreaId).arg(ac->areaId).arg(++xliveHeartBeatIndex).arg(ac->roomId));
    postData.insert("device", "[\"AUTO4115984068636104\",\"f5f08e2f-e4e3-4156-8127-616f79a17e1a\"]");
    postData.insert("ts", timestamp);
    postData.insert("ets", xliveHeartBeatEts);
    postData.insert("benchmark", xliveHeartBeatBenchmark);
    postData.insert("time", xliveHeartBeatInterval);
    postData.insert("ua", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.146 Safari/537.36");
    postData.insert("csrf_token", ac->csrf_token);
    postData.insert("csrf", ac->csrf_token);
    QJsonObject calcText;
    calcText.insert("t", postData);
    calcText.insert("r", xliveHeartBeatSecretRule);

    postJson(encServer, calcText, [=](QNetworkReply* reply){
        QByteArray repBa = reply->readAll();
        // qDebug() << "加密直播心跳包数据返回：" << repBa;
        // qDebug() << calcText;
        sendXliveHeartBeatX(MyJson(repBa).s("s"), timestamp);
    });
}

void MainWindow::sendXliveHeartBeatX(QString s, qint64 timestamp)
{
    QString url("https://live-trace.bilibili.com/xlive/data-interface/v1/x25Kn/X");

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "s=" + s; // 生成的签名
    datas << "id=" + QString("[%1,%2,%3,%4]")
             .arg(ac->parentAreaId).arg(ac->areaId).arg(xliveHeartBeatIndex).arg(ac->roomId);
    datas << "device=[\"AUTO4115984068636104\",\"f5f08e2f-e4e3-4156-8127-616f79a17e1a\"]";
    datas << "ets=" + snum(xliveHeartBeatEts);
    datas << "benchmark=" + xliveHeartBeatBenchmark;
    datas << "time=" + snum(xliveHeartBeatInterval);
    datas << "ts=" + snum(timestamp);
    datas << "ua=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.146 Safari/537.36";
    datas << "csrf_token=" + ac->csrf_token;
    datas << "csrf=" + ac->csrf_token;
    datas << "visit_id=";
    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    // qDebug() << "发送直播心跳X：" << url << ba;
    post(url, ba, [=](QJsonObject json){
        // qDebug() << "直播心跳返回：" << json;
        if (json.value("code").toInt() != 0)
        {
            QString message = json.value("message").toString();
            showError("发送直播心跳失败X", message);
            qCritical() << s8("warning: 发送直播心跳失败X：") << message << datas.join("&");
            return ;
        }

        /*{
            "code": 0,
            "message": "0",
            "ttl": 1,
            "data": {
                "heartbeat_interval": 60,
                "timestamp": 1612765598,
                "secret_rule": [2,5,1,4],
                "secret_key": "seacasdgyijfhofiuxoannn"
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        xliveHeartBeatBenchmark = data.value("secret_key").toString();
        xliveHeartBeatEts = qint64(data.value("timestamp").toDouble());
        xliveHeartBeatInterval = data.value("heartbeat_interval").toInt();
        xliveHeartBeatSecretRule = data.value("secret_rule").toArray();
        us->setValue("danmaku/todayHeartMinite", ++todayHeartMinite);
        ui->acquireHeartCheck->setToolTip("今日已领" + snum(todayHeartMinite/5) + "个小心心(" + snum(todayHeartMinite) + "分钟)");
        if (todayHeartMinite >= ui->heartTimeSpin->value())
            if (xliveHeartBeatTimer)
                xliveHeartBeatTimer->stop();
    });
}

/**
 * 获取房间信息
 * （已废弃）
 */
void MainWindow::getRoomInit()
{
    QString roomInitUrl = "https://api.live.bilibili.com/room/v1/Room/room_init?id=" + ac->roomId;
    connect(new NetUtil(roomInitUrl), &NetUtil::finished, this, [=](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "获取房间初始化出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qCritical() << "房间初始化返回结果不为0：" << json.value("message").toString();
            return ;
        }

        int realRoom = json.value("data").toObject().value("room_id").toInt();
        qInfo() << "真实房间号：" << realRoom;
    });
}

/**
 * 获取直播间信息，然后再开始连接
 * @param reconnect       是否是重新获取信息
 * @param reconnectCount  连接失败的重连次数
 */
void MainWindow::getRoomInfo(bool reconnect, int reconnectCount)
{
    switch (rt->livePlatform)
    {
    case Bilibili:
    {
        gettingRoom = true;
        QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByRoom?room_id=" + ac->roomId;
        get(url, [=](QJsonObject json) {
            if (json.value("code").toInt() != 0)
            {
                qCritical() << s8("获取房间信息返回结果不为0：") << json.value("message").toString();
                setRoomCover(QPixmap(":/bg/bg"));
                ui->connectStateLabel->setText("连接失败" + snum(reconnectCount+1));

                if (reconnectCount >= 5)
                {
                    ui->connectStateLabel->setText("无法连接");
                    return ;
                }
                qInfo() << "尝试重新获取房间信息：" << (reconnectCount + 1);
                QTimer::singleShot(5000, [=]{
                    getRoomInfo(reconnect, reconnectCount + 1);
                });
                return ;
            }

            QJsonObject dataObj = json.value("data").toObject();
            QJsonObject roomInfo = dataObj.value("room_info").toObject();
            QJsonObject anchorInfo = dataObj.value("anchor_info").toObject();

            // 获取房间信息
            ac->roomId = QString::number(roomInfo.value("room_id").toInt()); // 应当一样，但防止是短ID
            ui->roomIdEdit->setText(ac->roomId);
            ac->shortId = QString::number(roomInfo.value("short_id").toInt());
            ac->upUid = QString::number(static_cast<qint64>(roomInfo.value("uid").toDouble()));
            ac->liveStatus = roomInfo.value("live_status").toInt();
            int pkStatus = roomInfo.value("pk_status").toInt();
            if (danmakuWindow)
                danmakuWindow->setIds(ac->upUid.toLongLong(), ac->roomId.toLongLong());
            ac->roomTitle = roomInfo.value("title").toString();
            ac->upName = anchorInfo.value("base_info").toObject().value("uname").toString();
            ac->roomDescription = roomInfo.value("description").toString();
            ac->roomTags = roomInfo.value("tags").toString().split(",", QString::SkipEmptyParts);
            setWindowTitle(ac->roomTitle + " - " + ac->upName);
            tray->setToolTip(ac->roomTitle + " - " + ac->upName);
            if (ui->roomNameLabel->text().isEmpty() || ui->roomNameLabel->text() != warmWish)
                ui->roomNameLabel->setText(ac->roomTitle);
#ifdef ZUOQI_ENTRANCE
            fakeEntrance->setRoomName(ac->roomTitle);
#endif
            ui->upNameLabel->setText(ac->upName);
            ac->roomNews = dataObj.value("news_info").toObject().value("content").toString();

            // 设置房间描述
            setRoomDescription(ac->roomDescription);

            // 设置直播状态
            if (ac->liveStatus == 0)
            {
                ui->liveStatusButton->setText("未开播");
                if (ui->timerConnectServerCheck->isChecked() && !connectServerTimer->isActive())
                    connectServerTimer->start();
            }
            else if (ac->liveStatus == 1)
            {
                ui->liveStatusButton->setText("已开播");
            }
            else if (ac->liveStatus == 2)
            {
                ui->liveStatusButton->setText("轮播中");
            }
            else
            {
                ui->liveStatusButton->setText("未知状态" + snum(ac->liveStatus));
            }

            qInfo() << "房间信息: roomid=" << ac->roomId
                     << "  shortid=" << ac->shortId
                     << "  upName=" << ac->upName
                     << "  uid=" << ac->upUid;

            // 设置PK状态
            if (pkStatus)
            {
                QJsonObject battleInfo = dataObj.value("battle_info").toObject();
                QString pkId = QString::number(static_cast<qint64>(battleInfo.value("pk_id").toDouble()));
                if (pkId.toLongLong() > 0 && reconnect)
                {
                    // 这个 pk_status 不是 battle_type
                    pking = true;
                    // pkVideo = pkStatus == 2; // 注意：如果是匹配到后、开始前，也算是1/2,
                    setPkInfoById(ac->roomId, pkId);
                    qInfo() << "正在大乱斗：" << pkId << "   pk_status=" << pkStatus;
                }
            }
            else
            {
                QTimer::singleShot(5000, [=]{ // 延迟5秒，等待主播UID和机器人UID都获取到
                    if (ac->cookieUid == ac->upUid)
                        ui->actionJoin_Battle->setEnabled(true);
                });
            }

            // 发送心跳要用到的直播信息
            ac->areaId = snum(roomInfo.value("area_id").toInt());
            ac->areaName = roomInfo.value("area_name").toString();
            ac->parentAreaId = snum(roomInfo.value("parent_area_id").toInt());
            ac->parentAreaName = roomInfo.value("parent_area_name").toString();
            ui->roomAreaLabel->setText(ac->areaName);

            // 疑似在线人数
            int online = roomInfo.value("online").toInt();
            ui->popularityLabel->setText(snum(online));

            // 获取主播信息
            ac->currentFans = anchorInfo.value("relation_info").toObject().value("attention").toInt();
            ac->currentFansClub = anchorInfo.value("medal_info").toObject().value("fansclub").toInt();
    //        qInfo() << s8("粉丝数：") << ac->currentFans << s8("    粉丝团：") << ac->currentFansClub;
            ui->fansCountLabel->setText(snum(ac->currentFans));
            ui->fansClubCountLabel->setText(snum(ac->currentFansClub));
            // getFansAndUpdate();

            // 获取主播等级
            QJsonObject liveInfo = anchorInfo.value("live_info").toObject();
            ac->anchorLiveLevel = liveInfo.value("level").toInt();
            ac->anchorLiveScore = qint64(liveInfo.value("upgrade_score").toDouble());
            ac->anchorUpgradeScore = qint64(liveInfo.value("score").toDouble());
            // TODO: 显示主播等级和积分

            // 设置标签
            ui->tagsButtonGroup->initStringList(ac->roomTags);

            // 获取热门榜信息
            QJsonObject hotRankInfo = dataObj.value("hot_rank_info").toObject();
            int rank = hotRankInfo.value("rank").toInt();
            QString rankArea = hotRankInfo.value("area_name").toString();
            int countdown = hotRankInfo.value("countdown").toInt();
            if (!rankArea.isEmpty())
            {
                ui->roomRankLabel->setText(snum(rank));
                if (!rankArea.endsWith("榜"))
                    rankArea += "榜";
                ui->roomRankTextLabel->setText(rankArea);
                ui->roomRankTextLabel->setToolTip("当前总人数:" + snum(countdown));
            }

            // 获取直播排行榜
            QJsonObject areaRankInfo = dataObj.value("area_rank_info").toObject();
            ac->areaRank = areaRankInfo.value("areaRank").toObject().value("rank").toString();
            ac->liveRank = areaRankInfo.value("liveRank").toObject().value("rank").toString(); // ==anchor_info.live_info.rank
            ui->liveRankLabel->setText(ac->liveRank); // 直播榜（主播榜）
            ui->liveRankLabel->setToolTip("分区榜：" + ac->areaRank);

            // 看过
            ui->watchedLabel->setText(dataObj.value("watched_show").toObject().value("text_small").toString());

            // 获取大乱斗段位（现在可能是空了）
            QJsonObject battleRankEntryInfo = dataObj.value("battle_rank_entry_info").toObject();
            if (!battleRankEntryInfo.isEmpty())
            {
                ac->battleRankName = battleRankEntryInfo.value("rank_name").toString();
                QString battleRankUrl = battleRankEntryInfo.value("first_rank_img_url").toString(); // 段位图片
                ui->battleRankNameLabel->setText(ac->battleRankName);
                if (!ac->battleRankName.isEmpty())
                {
                    ui->battleInfoWidget->show();
                    get(battleRankUrl, [=](QNetworkReply* reply1){
                        QPixmap pixmap;
                        pixmap.loadFromData(reply1->readAll());
                        if (!pixmap.isNull())
                            pixmap = pixmap.scaledToHeight(ui->battleRankNameLabel->height() * 2, Qt::SmoothTransformation);
                        ui->battleRankIconLabel->setPixmap(pixmap);
                    });
                    upgradeWinningStreak(false);
                }
                else
                {
                    ui->battleInfoWidget->hide();
                }
            }
            else
            {
                getRoomBattleInfo();
            }

            // 异步获取房间封面
            getRoomCover(roomInfo.value("cover").toString());

            // 获取主播头像
            getUpInfo(ac->upUid);
            gettingRoom = false;
            if (!gettingUser)
                triggerCmdEvent("LOGIN_FINISHED", LiveDanmaku());
            updatePermission();

            // 判断房间，未开播则暂停连接，等待开播
            if (!isLivingOrMayliving())
                return ;

            // 开始工作
            if (isLiving())
                slotStartWork();

            if (!reconnect)
                return ;

            // 获取弹幕信息
            getDanmuInfo();

            // 录播
            if (ui->recordCheck->isChecked() && isLiving())
                startLiveRecord();

            // 获取礼物
            getGiftList();
        });
    }
        break;
    }

    if (reconnect)
        ui->connectStateLabel->setText("获取房间信息...");
}

void MainWindow::getRoomBattleInfo()
{
    get("https://api.live.bilibili.com/av/v1/Battle/anchorBattleRank",
        {"uid", ac->upUid, "_", snum(QDateTime::currentMSecsSinceEpoch())}, [=](MyJson json) {
        MyJson jdata = json.data();
        MyJson pkInfo = jdata.o("anchor_pk_info");
        ac->battleRankName = pkInfo.s("pk_rank_name");
        ui->battleRankNameLabel->setText(ac->battleRankName);
        QString battleRankUrl = pkInfo.s("first_rank_img_url");
        if (!ac->battleRankName.isEmpty())
        {
            ui->battleInfoWidget->show();
            get(battleRankUrl, [=](QNetworkReply* reply1){
                QPixmap pixmap;
                pixmap.loadFromData(reply1->readAll());
                if (!pixmap.isNull())
                    pixmap = pixmap.scaledToHeight(ui->battleRankNameLabel->height() * 2, Qt::SmoothTransformation);
                ui->battleRankIconLabel->setPixmap(pixmap);
            });
            upgradeWinningStreak(false);
            ui->battleInfoWidget->show();
        }
        else
        {
            ui->battleInfoWidget->hide();
        }
    });
}

bool MainWindow::isLivingOrMayliving()
{
    if (ui->timerConnectServerCheck->isChecked())
    {
        if (!isLiving()) // 未开播，等待下一次的检测
        {
            // 如果是开播前一段时间，则继续保持着连接
            int start = ui->startLiveHourSpin->value();
            int end = ui->endLiveHourSpin->value();
            int hour = QDateTime::currentDateTime().time().hour();
            int minu = QDateTime::currentDateTime().time().minute();
            bool abort = false;
            qint64 currentVal = hour * 3600000 + minu * 60000;
            qint64 nextVal = (currentVal + ui->timerConnectIntervalSpin->value() * 60000) % (24 * 3600000); // 0点
            if (start < end) // 白天档
            {
                qInfo() << "白天档" << currentVal << start * 3600000 << end * 3600000;
                if (nextVal >= start * 3600000
                        && currentVal <= end * 3600000)
                {
                    if (ui->doveCheck->isChecked()) // 今天鸽了
                    {
                        abort = true;
                        qInfo() << "今天鸽了";
                    }
                    // 即将开播
                }
                else // 直播时间之外
                {
                    qInfo() << "未开播，继续等待";
                    abort = true;
                    if (currentVal > end * 3600000 && ui->doveCheck->isChecked()) // 今天结束鸽鸽
                        ui->doveCheck->setChecked(false);
                }
            }
            else if (start > end) // 熬夜档
            {
                qInfo() << "晚上档" << currentVal << start * 3600000 << end * 3600000;
                if (currentVal + ui->timerConnectIntervalSpin->value() * 60000 >= start * 3600000
                        || currentVal <= end * 3600000)
                {
                    if (ui->doveCheck->isChecked()) // 今晚鸽了
                        abort = true;
                    // 即将开播
                }
                else // 直播时间之外
                {
                    qInfo() << "未开播，继续等待";
                    abort = true;
                    if (currentVal > end * 3600000 && currentVal < start * 3600000
                            && ui->doveCheck->isChecked())
                        ui->doveCheck->setChecked(false);
                }
            }

            if (abort)
            {
                qInfo() << "短期内不会开播，暂且断开连接";
                if (!connectServerTimer->isActive())
                    connectServerTimer->start();
                ui->connectStateLabel->setText("等待连接");

                // 如果正在连接或打算连接，却未开播，则断开
                if (socket->state() != QAbstractSocket::UnconnectedState)
                    socket->close();
                return false;
            }
        }
        else // 已开播，则停下
        {
            qInfo() << "开播，停止定时连接";
            if (connectServerTimer->isActive())
                connectServerTimer->stop();
            if (ui->doveCheck->isChecked())
                ui->doveCheck->setChecked(false);
            return true;
        }
    }
    return true;
}

bool MainWindow::isWorking() const
{
    if (localDebug)
        return false;
    return (shallAutoMsg() && (ui->autoSendWelcomeCheck->isChecked() || ui->autoSendGiftCheck->isChecked() || ui->autoSendAttentionCheck->isChecked()));
}

void MainWindow::updatePermission()
{
    permissionLevel = 0;
    if (gettingRoom || gettingUser)
        return ;
    QString userId = ac->cookieUid;
    get(serverPath + "pay/isVip", {"room_id", ac->roomId, "user_id", userId}, [=](MyJson json) {
        MyJson jdata = json.data();
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        qint64 deadline = 0;
        if (!jdata.value("RR").isNull())
        {
            MyJson info = jdata.o("RR");
            if (info.l("deadline") > timestamp)
            {
                permissionLevel = qMax(permissionLevel, info.i("vipLevel"));
                deadline = qMax(deadline, info.l("deadline"));
                permissionType[1] = true;
            }
        }
        if (!jdata.value("ROOM").isNull())
        {
            MyJson info = jdata.o("ROOM");
            if (info.l("deadline") > timestamp)
            {
                permissionLevel = qMax(permissionLevel, info.i("vipLevel"));
                deadline = qMax(deadline, info.l("deadline"));
                permissionType[2] = true;
            }
        }
        if (!jdata.value("ROBOT").isNull())
        {
            MyJson info = jdata.o("ROBOT");
            if (info.l("deadline") > timestamp)
            {
                permissionLevel = qMax(permissionLevel, info.i("vipLevel"));
                deadline = qMax(deadline, info.l("deadline"));
                permissionType[3] = true;
            }
        }
        if (!jdata.value("GIFT").isNull())
        {
            MyJson info = jdata.o("GIFT");
            if (info.l("deadline") > timestamp)
            {
                permissionLevel = qMax(permissionLevel, info.i("vipLevel"));
                deadline = qMax(deadline, info.l("deadline"));
                permissionType[10] = true;
            }
        }

        this->permissionDeadline = deadline;
        if (permissionLevel)
        {
            ui->droplight->setText(permissionText);
            ui->droplight->adjustMinimumSize();
            ui->droplight->setNormalColor(themeSbg);
            ui->droplight->setTextColor(themeSfg);
            ui->droplight->setToolTip("剩余时长：" + snum((deadline - timestamp) / (24 * 3600)) + "天");
            ui->vipExtensionButton->hide();
            ui->heartTimeSpin->setMaximum(1440); // 允许挂机24小时

            qint64 tm = deadline - timestamp;
            if (tm > 24 * 3600)
                tm = 24 * 3600;
            permissionTimer->setInterval(tm * 1000);
            permissionTimer->start();
        }
        else
        {
            ui->droplight->setText("免费版");
            ui->droplight->setNormalColor(Qt::white);
            ui->droplight->setTextColor(Qt::black);
            ui->droplight->setToolTip("点击解锁新功能");
            ui->vipExtensionButton->show();
            ui->heartTimeSpin->setMaximum(120); // 仅允许刚好获取完小心心
            permissionTimer->stop();
        }

        readConfig2();
    });
}

int MainWindow::hasPermission()
{
    return justStart || permissionLevel;
}

/**
 * 新的一天到来了
 * 可能是启动时就是新的一天
 * 也可能是运行时到了第二天
 */
void MainWindow::processNewDay()
{
    us->setValue("danmaku/todayHeartMinite", todayHeartMinite = 0);
}

void MainWindow::getRoomCover(QString url)
{
    get(url, [=](QNetworkReply* reply1){
        QByteArray jpegData = reply1->readAll();

        QPixmap pixmap;
        pixmap.loadFromData(jpegData);
        setRoomCover(pixmap);
    });
}

void MainWindow::setRoomCover(const QPixmap& pixmap)
{
    roomCover = pixmap; // 原图
    if (roomCover.isNull())
        return ;

    adjustCoverSizeByRoomCover(roomCover);

    /* int w = ui->roomCoverLabel->width();
    if (w > ui->tabWidget->contentsRect().width())
        w = ui->tabWidget->contentsRect().width();
    pixmap = pixmap.scaledToWidth(w, Qt::SmoothTransformation);
    ui->roomCoverLabel->setPixmap(getRoundedPixmap(pixmap));
    ui->roomCoverLabel->setMinimumSize(1, 1); */

    // 设置程序主题
    QColor bg, fg, sbg, sfg;
    auto colors = ImageUtil::extractImageThemeColors(roomCover.toImage(), 7);
    ImageUtil::getBgFgSgColor(colors, &bg, &fg, &sbg, &sfg);
    prevPa = BFSColor::fromPalette(palette());
    currentPa = BFSColor(QList<QColor>{bg, fg, sbg, sfg});
    QPropertyAnimation* ani = new QPropertyAnimation(this, "paletteProg");
    ani->setStartValue(0);
    ani->setEndValue(1.0);
    ani->setDuration(500);
    connect(ani, &QPropertyAnimation::valueChanged, this, [=](const QVariant& val){
        setRoomThemeByCover(val.toDouble());
    });
    connect(ani, SIGNAL(finished()), ani, SLOT(deleteLater()));
    ani->start();

    // 设置主要界面主题
    ui->tabWidget->setBg(roomCover);
}

void MainWindow::setRoomThemeByCover(double val)
{
    double d = val;
    BFSColor bfs = prevPa + (currentPa - prevPa) * d;
    QColor bg, fg, sbg, sfg;
    bfs.toColors(&bg, &fg, &sbg, &sfg);

    themeBg = bg;
    themeFg = fg;
    themeSbg = sbg;
    themeSfg = sfg;
    ListItemInterface::triggerColor = sbg;

    QPalette pa;
    pa.setColor(QPalette::Window, bg);
    pa.setColor(QPalette::Background, bg);
    pa.setColor(QPalette::Button, bg);
    pa.setColor(QPalette::Base, bg);

    pa.setColor(QPalette::Foreground, fg);
    pa.setColor(QPalette::Text, fg);
    pa.setColor(QPalette::ButtonText, fg);
    pa.setColor(QPalette::WindowText, fg);

    // 设置背景透明
    QString labelStyle = "color:" + QVariant(fg).toString() + ";";
    statusLabel->setStyleSheet(labelStyle);
    ui->roomNameLabel->setStyleSheet(labelStyle + "font-size: 18px;");

    pa.setColor(QPalette::Highlight, sbg);
    pa.setColor(QPalette::HighlightedText, sfg);
    setPalette(pa);
    // setStyleSheet("QMainWindow{background:"+QVariant(bg).toString()+"}");
    ui->menubar->setStyleSheet("QMenuBar:item{background:transparent;}QMenuBar{background:transparent; color:"+QVariant(fg).toString()+"}");
    roomIdBgWidget->setStyleSheet("#roomIdBgWidget{ background: " + QVariant(themeSbg).toString() + "; border-radius: " + snum(roomIdBgWidget->height()/2) + "px; }");
    sideButtonList.at(ui->stackedWidget->currentIndex())->setNormalColor(sbg);
    ui->tagsButtonGroup->setMouseColor([=]{QColor c = themeSbg; c.setAlpha(127); return c;}(),
                                       [=]{QColor c = themeSbg; c.setAlpha(255); return c;}());
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setNormalColor(sbg);
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setTextColor(sfg);
    ui->showLiveDanmakuWindowButton->setBgColor(sbg);
    ui->showLiveDanmakuWindowButton->setTextColor(sfg);
    ui->SendMsgButton->setTextColor(fg);
    ui->showOrderPlayerButton->setNormalColor(sbg);
    ui->showOrderPlayerButton->setTextColor(sfg);
    // ui->addMusicToLiveButton->setBorderColor(sbg); // 感觉太花哨了
    appendListItemButton->setBgColor(sbg);
    appendListItemButton->setIconColor(sfg);
    if (hasPermission())
    {
        ui->droplight->setNormalColor(sbg);
        ui->droplight->setTextColor(sfg);
    }

    // 纯文字label的
    pa = ui->appNameLabel->palette();
    pa.setColor(QPalette::Foreground, fg);
    pa.setColor(QPalette::Text, fg);
    pa.setColor(QPalette::ButtonText, fg);
    pa.setColor(QPalette::WindowText, fg);
    ui->appNameLabel->setPalette(pa);
    ui->appDescLabel->setPalette(pa);

    QColor bgTrans = sbg;
    int alpha = (3 * ( bgTrans.red()) * (bgTrans.red())
                 + 4 * (bgTrans.green()) * (bgTrans.green())
                 + 2 * (bgTrans.blue()) * (bgTrans.blue()))
                 / 9 / 255;
    alpha = 16 + alpha / 4; // 16~80
    bgTrans.setAlpha(alpha);
    QString cardStyleSheet = "{ background: " + QVariant(bgTrans).toString() + "; border: none; border-radius: " + snum(rt->fluentRadius) + " }";
    ui->guardCountCard->setStyleSheet("#guardCountCard" + cardStyleSheet);
    ui->hotCountCard->setStyleSheet("#hotCountCard" + cardStyleSheet);
    ui->watchedCountCard->setStyleSheet("#watchedCountCard" + cardStyleSheet);
    ui->robotSendCountCard->setStyleSheet("#robotSendCountCard" + cardStyleSheet);

    alpha = (3 * ( themeGradient.red()) * (themeGradient.red())
                 + 4 * (themeGradient.green()) * (themeGradient.green())
                 + 2 * (themeGradient.blue()) * (themeGradient.blue()))
                 / 9 / 255;
    alpha = 16 + alpha / 4; // 16~80
    themeGradient = themeBg;
    themeGradient.setAlpha(alpha);
}

void MainWindow::adjustCoverSizeByRoomCover(QPixmap pixmap)
{
    // 计算尺寸
    int w = ui->roomInfoMainWidget->width();
    int maxH = this->height() * (1 - 0.618); // 封面最大高度
    pixmap = pixmap.scaledToWidth(w, Qt::SmoothTransformation);
    int showH = pixmap.height(); // 当前高度下图片应当显示的高度
    if (showH > maxH) // 不能超过原始高度
        showH = maxH;
    int spacingH = showH - (ui->upNameLabel->y() - ui->upHeaderLabel->y()) - ui->roomCoverSpacingLabel->y(); // 间隔控件的高度
    spacingH -= 18;
    ui->roomCoverSpacingLabel->setFixedHeight(spacingH);
    ui->roomInfoMainWidget->layout()->activate(); // 不激活一下布局的话，在启动时会有问题

    // 因为布局原因，实际上显示的不一定是完整图片所需的尺寸（至少宽度是够的）
    QPixmap& p = pixmap;
    int suitH = ui->upNameLabel->y() - 6; // 最适合的高度
    if (suitH < p.height())
        p = p.copy(0, (p.height() - suitH) / 2, p.width(), suitH);
    roomCoverLabel->setPixmap(getTopRoundedPixmap(p, rt->fluentRadius));
    roomCoverLabel->resize(p.size());
    roomCoverLabel->lower();
}

void MainWindow::adjustRoomIdWidgetPos()
{
    QSize editSize = ui->roomIdEdit->size();
    QSize sz = QSize(ui->roomIdSpacingWidget->x() + editSize.width() + editSize.height() * 2, editSize.height() + roomIdBgWidget->layout()->margin() * 2);
    roomIdBgWidget->setFixedSize(sz);
    ui->roomIdSpacingWidget->setFixedHeight(sz.height() - 9); // 有个间距
    ui->roomIdSpacingWidget_3->setFixedHeight(sz.height() - 9); // 有个间距
    roomIdBgWidget->move(ui->sideBarWidget->x() + ui->sideBarWidget->width() / 20, ui->sideBarWidget->y());
    roomIdBgWidget->setStyleSheet("#roomIdBgWidget{ background: " + QVariant(themeSbg).toString() + "; border-radius: " + snum(roomIdBgWidget->height()/2) + "px; }");
    ui->sideBarWidget->raise();
    roomIdBgWidget->stackUnder(ui->sideBarWidget);
}

void MainWindow::showRoomIdWidget()
{
    QPropertyAnimation* ani = new QPropertyAnimation(roomIdBgWidget, "pos");
    if (roomIdBgWidget->isHidden())
        ani->setStartValue(QPoint(-roomIdBgWidget->width(), roomIdBgWidget->y()));
    else
        ani->setStartValue(roomIdBgWidget->pos());
    ani->setEndValue(roomIdBgWidget->pos());
    ani->setDuration(300);
    ani->setEasingCurve(QEasingCurve::OutCirc);
    connect(ani, &QPropertyAnimation::finished, this, [=]{
        ani->deleteLater();
    });
    roomIdBgWidget->show();
    ani->start();
}

void MainWindow::hideRoomIdWidget()
{
    if (roomIdBgWidget->isHidden())
        return ;
    QPropertyAnimation* ani = new QPropertyAnimation(roomIdBgWidget, "pos");
    ani->setStartValue(roomIdBgWidget->pos());
    ani->setEndValue(QPoint(-roomIdBgWidget->width(), roomIdBgWidget->y()));
    ani->setDuration(300);
    ani->setEasingCurve(QEasingCurve::OutCirc);
    connect(ani, &QPropertyAnimation::finished, this, [=]{
        roomIdBgWidget->hide();
        ani->deleteLater();
    });
    ani->start();
}

QPixmap MainWindow::getRoundedPixmap(const QPixmap &pixmap) const
{
    QPixmap dest(pixmap.size());
    dest.fill(Qt::transparent);
    QPainter painter(&dest);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QRect rect = QRect(0, 0, pixmap.width(), pixmap.height());
    QPainterPath path;
    path.addRoundedRect(rect, 5, 5);
    painter.setClipPath(path);
    painter.drawPixmap(rect, pixmap);
    return dest;
}

QPixmap MainWindow::getTopRoundedPixmap(const QPixmap &pixmap, int radius) const
{
    QPixmap dest(pixmap.size());
    dest.fill(Qt::transparent);
    QPainter painter(&dest);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QRect rect = QRect(0, 0, pixmap.width(), pixmap.height());
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    QPainterPath cutPath;
    cutPath.addRect(0, radius, rect.width(), rect.height() - radius);
    path -= cutPath;
    path.addRect(0, radius, rect.width(), rect.height() - radius);
    painter.setClipPath(path);
    painter.drawPixmap(rect, pixmap);
    return dest;
}

void MainWindow::getUpInfo(const QString &uid)
{
    QString url = "http://api.bilibili.com/x/space/acc/info?mid=" + uid;
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("获取主播信息返回结果不为0：") << json.value("message").toString();
            return ;
        }
        QJsonObject data = json.value("data").toObject();

        // 设置签名
        QString sign = data.value("sign").toString();
        {
            // 删除首尾空
            sign.replace(QRegularExpression("\n[\n\r ]*\n"), "\n")
                    .replace(QRegularExpression("^[\n\r ]+"), "")
                    .replace(QRegularExpression("[\n\r ]+$"), "");
            ui->upLevelLabel->setText(sign.trimmed());
        }

        // 开始下载头像
        QString face = data.value("face").toString();
        downloadUpFace(face);
    });
}

void MainWindow::downloadUpFace(const QString &faceUrl)
{
    get(faceUrl, [=](QNetworkReply* reply){
        QByteArray jpegData = reply->readAll();

        QPixmap pixmap;
        pixmap.loadFromData(jpegData);
        upFace = pixmap; // 原图
        pixmap = toCirclePixmap(pixmap); // 圆图

        // 设置到窗口图标
        QPixmap face = isLiving() ? toLivingPixmap(pixmap) : pixmap;
        setWindowIcon(face);
        tray->setIcon(face);

        // 设置到UP头像
        face = upFace.scaled(ui->upHeaderLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap circle = toCirclePixmap(face);
        ui->upHeaderLabel->setPixmap(circle);
        if (musicWindow)
        {
            musicWindow->setTitleIcon(circle);
        }
    });
}

/**
 * 变成圆形的图片
 */
QPixmap MainWindow::toCirclePixmap(const QPixmap &pixmap) const
{
    if (pixmap.isNull())
        return pixmap;
    int side = qMin(pixmap.width(), pixmap.height());
    QPixmap rp = QPixmap(side, side);
    rp.fill(Qt::transparent);
    QPainter painter(&rp);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addEllipse(0, 0, side, side);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, side, side, pixmap);
    return rp;
}

/**
 * 给直播中的头像添加绿点
 */
QPixmap MainWindow::toLivingPixmap(QPixmap pixmap) const
{
    if (pixmap.isNull())
        return pixmap;
    QPainter painter(&pixmap);
    int wid = qMin(pixmap.width(), pixmap.height()) / 3;
    QRect rect(pixmap.width() - wid, pixmap.height() - wid, wid, wid);
    QPainterPath path;
    path.addEllipse(rect);
    painter.fillPath(path, Qt::green);
    return pixmap;
}

/**
 * 这是真正开始连接的
 * 获取到长链的信息
 */
void MainWindow::getDanmuInfo()
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo?id="+ac->roomId+"&type=0";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray dataBa = reply->readAll();
        manager->deleteLater();
        delete request;
        reply->deleteLater();

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(dataBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "获取弹幕信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("获取弹幕信息返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        ac->cookieToken = data.value("token").toString();
        QJsonArray hostArray = data.value("host_list").toArray();
        hostList.clear();
        foreach (auto val, hostArray)
        {
            QJsonObject o = val.toObject();
            hostList.append(HostInfo{
                                o.value("host").toString(),
                                o.value("port").toInt(),
                                o.value("wss_port").toInt(),
                                o.value("ws_port").toInt(),
                            });
        }
        SOCKET_DEB << s8("getDanmuInfo: host数量=") << hostList.size() << "  token=" << ac->cookieToken;

        startMsgLoop();

        updateExistGuards(0);
        updateOnlineGoldRank();
    });
    manager->get(*request);
    ui->connectStateLabel->setText("获取弹幕信息...");
}

void MainWindow::getFansAndUpdate()
{
    QString url = "http://api.bilibili.com/x/relation/followers?vmid=" + ac->upUid;
    get(url, [=](QJsonObject json){
        QJsonArray list = json.value("data").toObject().value("list").toArray();
        QList<FanBean> newFans;
        foreach (QJsonValue val, list)
        {
            QJsonObject fan = val.toObject();
            qint64 mid = static_cast<qint64>(fan.value("mid").toDouble());
            int attribute = fan.value("attribute").toInt(); // 三位数：1-1-0 被关注-已关注-未知
            if (attribute == 0) // 0是什么意思啊，不懂诶……应该是关注吧？
                attribute = 2;
            qint64 mtime = static_cast<qint64>(fan.value("mtime").toDouble()); // 这是秒吧？
            QString uname = fan.value("uname").toString();

            newFans.append(FanBean{mid, uname, attribute & 2, mtime});
//qInfo() << "    粉丝信息：" << mid << uname << attribute << QDateTime::fromSecsSinceEpoch(mtime);
        }

//qInfo() << "    现有粉丝数(第一页)：" << newFans.size();

        // 第一次加载
        if (!fansList.size())
        {
            fansList = newFans;
            return ;
        }

        // 进行比较 新增关注 的 取消关注
        // 先找到原先最后关注并且还在的，即：旧列.index == 新列.find
        int index = -1; // 旧列索引
        int find = -1;  // 新列索引
        while (++index < fansList.size())
        {
            FanBean fan = fansList.at(index);
            for (int j = 0; j < newFans.size(); j++)
            {
                FanBean nFan = newFans.at(j);
                if (fan.mid == nFan.mid)
                {
                    find = j;
                    break;
                }
            }
            if (find >= 0)
                break;
        }

        // 取消关注（只支持最新关注的，不是专门做的，只是顺带）
        if (!fansList.size())
        {}
        else if (index >= fansList.size()) // 没有被关注过，或之前关注的全部取关了？
        {
            qInfo() << s8("没有被关注过，或之前关注的全部取关了？");
        }
        else // index==0没有人取关，index>0则有人取关
        {
            for (int i = 0; i < index; i++)
            {
                FanBean fan = fansList.at(i);
                qInfo() << s8("取消关注：") << fan.uname << QDateTime::fromSecsSinceEpoch(fan.mtime);
                appendNewLiveDanmaku(LiveDanmaku(fan.uname, fan.mid, false, QDateTime::fromSecsSinceEpoch(fan.mtime)));
            }
            while (index--)
                fansList.removeFirst();
        }

        // 新增关注
        for (int i = find-1; i >= 0; i--)
        {
            FanBean fan = newFans.at(i);
            qInfo() << s8("新增关注：") << fan.uname << QDateTime::fromSecsSinceEpoch(fan.mtime);
            LiveDanmaku danmaku(fan.uname, fan.mid, true, QDateTime::fromSecsSinceEpoch(fan.mtime));
            appendNewLiveDanmaku(danmaku);

            if (i == 0) // 只发送第一个（其他几位，对不起了……）
            {
                if (!justStart && ui->autoSendAttentionCheck->isChecked())
                {
                    sendAttentionThankIfNotRobot(danmaku);
                }
                else
                {
                    judgeRobotAndMark(danmaku);
                }
            }
            else
            {
                judgeRobotAndMark(danmaku);
            }

            fansList.insert(0, fan);
        }

    });
}

void MainWindow::setPkInfoById(QString roomId, QString pkId)
{
    /*{
        "code": 0,
        "msg": "ok",
        "message": "ok",
        "data": {
            "battle_type": 2,
            "match_type": 8,
            "status_msg": "",
            "pk_id": 200456233,
            "season_id": 31,
            "status": 201,
            "match_status": 0,
            "match_max_time": 300,
            "match_end_time": 0,
            "pk_pre_time": 1611844801,
            "pk_start_time": 1611844811,
            "pk_frozen_time": 1611845111,
            "pk_end_time": 1611845121,
            "timestamp": 1611844942,
            "final_hit_votes": 0,
            "pk_votes_add": 0,
            "pk_votes_name":"乱斗值",
            "pk_votes_type": 0,
            "cdn_id": 72,
            "init_info": {
                "room_id": 22580649,
                "uid": 356772517,
                "uname":"呱什么呱",
                "face": "https://i1.hdslb.com/bfs/face/3a4d357fdf88d73110b6b0cb31f3417f70c785af.jpg",
                "votes": 0,
                "final_hit_status": 0,
                "resist_crit_status": 0,
                "resist_crit_num": "",
                "best_uname": "",
                "winner_type": 0,
                "end_win_task": null
            },
            "match_info": {
                "room_id": 4720666,
                "uid": 13908357,
                "uname":"娇羞的蘑菇",
                "face": "https://i1.hdslb.com/bfs/face/180d0e87a0e88cb6c04ce6504c3f04003dd77392.jpg",
                "votes": 0,
                "final_hit_status": 0,
                "resist_crit_status": 0,
                "resist_crit_num": "",
                "best_uname": "",
                "winner_type": 0,
                "end_win_task": null
            }
        }
    }*/

    QString url = "https://api.live.bilibili.com/av/v1/Battle/getInfoById?pk_id="+pkId+"&roomid=" + roomId;\
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qWarning() << "PK查询结果不为0：" << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        // pk_pre_time  pk_start_time  pk_frozen_time  pk_end_time
        QJsonObject pkData = json.value("data").toObject();
        qint64 pkStartTime = static_cast<qint64>(pkData.value("pk_start_time").toDouble());
        pkEndTime = static_cast<qint64>(pkData.value("pk_frozen_time").toDouble());

        QJsonObject initInfo = pkData.value("init_info").toObject();
        QJsonObject matchInfo = pkData.value("match_info").toObject();
        if (snum(qint64(matchInfo.value("room_id").toDouble())) == roomId)
        {
            QJsonObject temp = initInfo;
            initInfo = matchInfo;
            matchInfo = temp;
        }

        pkRoomId = snum(qint64(matchInfo.value("room_id").toDouble()));
        pkUid = snum(qint64(matchInfo.value("uid").toDouble()));
        pkUname = matchInfo.value("uname").toString();
        myVotes = initInfo.value("votes").toInt();
        matchVotes = matchInfo.value("votes").toInt();
        pkBattleType = pkData.value("battle_type").toInt();
        pkVideo = pkBattleType == 2;

        pkTimer->start();
        if (danmakuWindow)
        {
            danmakuWindow->showStatusText();
            danmakuWindow->setToolTip(pkUname);
            danmakuWindow->setPkStatus(1, pkRoomId.toLongLong(), pkUid.toLongLong(), pkUname);
        }
        ui->actionShow_PK_Video->setEnabled(true);

        qint64 currentTime = QDateTime::currentSecsSinceEpoch();
        // 已经开始大乱斗
        if (currentTime > pkStartTime) // !如果是刚好开始，可能不能运行下面的，也可能不会触发"PK_START"，不管了
        {
            if (pkChuanmenEnable)
            {
                connectPkRoom();
            }

            // 监听尾声
            qint64 deltaEnd = pkEndTime - currentTime;
            if (deltaEnd*1000 > pkJudgeEarly)
            {
                pkEndingTimer->start(deltaEnd*1000 - pkJudgeEarly);

                // 怕刚好结束的一瞬间，没有收到大乱斗的消息
                QTimer::singleShot(qMax(0, int(deltaEnd)), [=]{
                    pkEnding = false;
                    pkVoting = 0;
                });
            }
        }
    });
}

/**
 * socket连接到直播间，但不一定已开播
 */
void MainWindow::startMsgLoop()
{
    // 开启保存房间弹幕
    if (ui->saveDanmakuToFileCheck && !danmuLogFile)
        startSaveDanmakuToFile();

    // 开始连接
    // int hostUseIndex = 0; // 循环遍历可连接的host（意思一下，暂时未使用）
    if (hostUseIndex < 0 || hostUseIndex >= hostList.size())
        hostUseIndex = 0;
    HostInfo hostServer = hostList.at(hostUseIndex);
    us->setValue("live/hostIndex", hostUseIndex);
    hostUseIndex++; // 准备用于尝试下一次的遍历，连接成功后-1

    QString host = QString("wss://%1:%2/sub").arg(hostServer.host).arg(hostServer.wss_port);
    SOCKET_DEB << "hostServer:" << host << "   hostIndex:" << (hostUseIndex-1);

    // 设置安全套接字连接模式（不知道有啥用）
    QSslConfiguration config = socket->sslConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    config.setProtocol(QSsl::TlsV1SslV3);
    socket->setSslConfiguration(config);

    socket->open(host);
}

void MainWindow::sendVeriPacket(QWebSocket* socket, QString roomId, QString token)
{
    QByteArray ba;
    ba.append("{\"uid\": 0, \"roomid\": "+roomId+", \"protover\": 2, \"platform\": \"web\", \"clientver\": \"1.14.3\", \"type\": 2, \"key\": \""+token+"\"}");
    ba = BiliApiUtil::makePack(ba, OP_AUTH);
    SOCKET_DEB << "发送认证包：" << ba;
    socket->sendBinaryMessage(ba);
}

/**
 * 每隔半分钟发送一次心跳包
 */
void MainWindow::sendHeartPacket()
{
    QByteArray ba;
    ba.append("[object Object]");
    ba = BiliApiUtil::makePack(ba, OP_HEARTBEAT);
//    SOCKET_DEB << "发送心跳包：" << ba;
    socket->sendBinaryMessage(ba);
}

QString MainWindow::getLocalNickname(qint64 uid) const
{
    if (us->localNicknames.contains(uid))
        return us->localNicknames.value(uid);
    return "";
}

void MainWindow::analyzeMsgAndCd(QString& msg, int &cd, int &channel) const
{
    QRegularExpression re("^\\s*\\(cd(\\d{1,2}):(\\d+)\\)");
    QRegularExpressionMatch match;
    if (msg.indexOf(re, 0, &match) == -1)
        return ;
    QStringList caps = match.capturedTexts();
    QString full = caps.at(0);
    QString chann = caps.at(1);
    QString val = caps.at(2);
    msg = msg.right(msg.length() - full.length());
    cd = val.toInt() * 1000;
    channel = chann.toInt();
}

/**
 * 支持变量名
 */
QString MainWindow::processTimeVariants(QString msg) const
{
    // 早上/下午/晚上 - 好呀
    int hour = QTime::currentTime().hour();
    if (msg.contains("%hour%"))
    {
        QString rst;
        if (hour <= 3)
            rst = "晚上";
        else if (hour <= 5)
            rst = "凌晨";
        else if (hour < 11)
            rst = "早上";
        else if (hour <= 13)
            rst = "中午";
        else if (hour <= 17)
            rst = "下午";
        else if (hour <= 24)
            rst = "晚上";
        else
            rst = "今天";
        msg = msg.replace("%hour%", rst);
    }

    // 早上好、早、晚上好-
    if (msg.contains("%greet%"))
    {
        QStringList rsts;
        rsts << "你好";
        if (hour <= 3)
            rsts << "晚上好";
        else if (hour <= 5)
            rsts << "凌晨好";
        else if (hour < 11)
            rsts << "早上好" << "早" << "早安";
        else if (hour <= 13)
            rsts << "中午好";
        else if (hour <= 16)
            rsts << "下午好";
        else if (hour <= 24)
            rsts << "晚上好";
        int r = qrand() % rsts.size();
        msg = msg.replace("%greet%", rsts.at(r));
    }

    // 全部打招呼
    if (msg.contains("%all_greet%"))
    {
        QStringList sl{"啊", "呀"};
        int r = qrand() % sl.size();
        QString tone = sl.at(r);
        QStringList rsts;
        rsts << "您好"+tone
             << "你好"+tone;
        if (hour <= 3)
            rsts << "晚上好"+tone;
        else if (hour <= 5)
            rsts << "凌晨好"+tone;
        else if (hour < 11)
            rsts << "早上好"+tone << "早"+tone;
        else if (hour <= 13)
            rsts << "中午好"+tone;
        else if (hour <= 16)
            rsts << "下午好"+tone;
        else if (hour <= 24)
            rsts << "晚上好"+tone;

        if (hour >= 6 && hour <= 8)
            rsts << "早饭吃了吗";
        else if (hour >= 11 && hour <= 12)
            rsts << "午饭吃了吗";
        else if (hour >= 17 && hour <= 18)
            rsts << "晚饭吃了吗";

        if ((hour >= 23 && hour <= 24)
                || (hour >= 0 && hour <= 3))
            rsts << "还没睡"+tone << "怎么还没睡";
        else if (hour >= 3 && hour <= 5)
            rsts << "通宵了吗";

        r = qrand() % rsts.size();
        msg = msg.replace("%all_greet%", rsts.at(r));
    }

    // 语气词
    if (msg.contains("%tone%"))
    {
        QStringList sl{"啊", "呀"};
        int r = qrand() % sl.size();
        msg = msg.replace("%tone%", sl.at(r));
    }
    if (msg.contains("%lela%"))
    {
        QStringList sl{"了", "啦"};
        int r = qrand() % sl.size();
        msg = msg.replace("%lela%", sl.at(r));
    }

    // 标点
    if (msg.contains("%punc%"))
    {
        QStringList sl{"~", "！"};
        int r = qrand() % sl.size();
        msg = msg.replace("%punc%", sl.at(r));
    }

    // 语气词或标点
    if (msg.contains("%tone/punc%"))
    {
        QStringList sl{"啊", "呀", "~", "！"};
        int r = qrand() % sl.size();
        msg = msg.replace("%tone/punc%", sl.at(r));
    }

    return msg;
}

/**
 * 获取可以发送的代码的列表
 * @param plainText 为替换变量的纯文本，允许使用\\n分隔的单行
 * @param danmaku   弹幕变量
 * @return          一行一条执行序列，带发送选项
 */
QStringList MainWindow::getEditConditionStringList(QString plainText, LiveDanmaku danmaku)
{
    // 替换变量，整理为一行一条执行序列
    plainText = processDanmakuVariants(plainText, danmaku);
    CALC_DEB << "处理变量之后：" << plainText;
    lastConditionDanmu.append(plainText);
    if (lastConditionDanmu.size() > debugLastCount)
        lastConditionDanmu.removeFirst();

    // 寻找条件为true的
    QStringList lines = plainText.split("\n", QString::SkipEmptyParts);
    QStringList result;
    for (int i = 0; i < lines.size(); i++)
    {
        QString line = lines.at(i);
        line = processMsgHeaderConditions(line);
        CALC_DEB << "取条件后：" << line << "    原始：" << lines.at(i);
        if (!line.isEmpty())
            result.append(line.trimmed());
    }

    // 判断超过长度的
    if (removeLongerRandomDanmaku)
    {
        for (int i = 0; i < result.size() && result.size() > 1; i++)
        {
            QString s = result.at(i);
            if (s.contains(">") || s.contains("\\n")) // 包含多行或者命令的，不需要或者懒得判断长度
                continue;
            s = s.replace(QRegExp("^\\s+\\(\\s*[\\w\\d: ,]*\\s*\\)"), "").replace("*", "").trimmed(); // 去掉发送选项
            // s = s.replace(QRegExp("^\\s+\\(\\s*cd\\d+\\s*:\\s*\\d+\\s*\\)"), "").replace("*", "").trimmed();
            if (s.length() > danmuLongest && !s.contains("%"))
            {
                if (debugPrint)
                    localNotify("[去掉过长候选：" + s + "]");
                result.removeAt(i--);
            }
        }
    }

    // 查看是否优先级
    auto hasPriority = [=](const QStringList& sl){
        for (int i = 0; i < sl.size(); i++)
            if (sl.at(i).startsWith("*"))
                return true;
        return false;
    };

    // 去除优先级
    while (hasPriority(result))
    {
        for (int i = 0; i < result.size(); i++)
        {
            if (!result.at(i).startsWith("*"))
                result.removeAt(i--);
            else
                result[i] = result.at(i).right(result.at(i).length()-1);
        }
    }
    CALC_DEB << "condition result:" << result;
    lastCandidateDanmaku.append(result.join("\n"));
    if (lastCandidateDanmaku.size() > debugLastCount)
        lastCandidateDanmaku.removeFirst();

    return result;
}

/**
 * 处理用户信息中蕴含的表达式
 * 用户信息、弹幕、礼物等等
 */
QString MainWindow::processDanmakuVariants(QString msg, const LiveDanmaku& danmaku)
{
    QRegularExpressionMatch match;
    QRegularExpression re;

    // 去掉注释
    re = QRegularExpression("(?<!:)//.*?(?=\\n|$|\\\\n)");
    msg.replace(re, "");

    // 软换行符
    re = QRegularExpression("\\s*\\\\\\s*\\n\\s*");
    msg.replace(re, "");

    CALC_DEB << "有效代码：" << msg;

    // 自动回复传入的变量
    re = QRegularExpression("%\\$(\\d+)%");
    while (msg.indexOf(re, 0, &match) > -1)
    {
        QString _var = match.captured(0);
        int i = match.captured(1).toInt();
        QString text = danmaku.getArgs(i);
        msg.replace(_var, text);
    }

    // 自定义变量
    for (auto it = us->customVariant.begin(); it != us->customVariant.end(); ++it)
    {
        msg.replace(it->first, it->second);
    }

    // 翻译
    for (auto it = us->variantTranslation.begin(); it != us->variantTranslation.end(); ++it)
    {
        msg.replace(it->first, it->second);
    }

    // 招呼变量（固定文字，随机内容）
    msg = processTimeVariants(msg);

    // 弹幕变量、环境变量（固定文字）
    re = QRegularExpression("%[\\w_]+%");
    int matchPos = 0;
    bool ok;
    while ((matchPos = msg.indexOf(re, matchPos, &match)) > -1)
    {
        QString mat = match.captured(0);
        QString rpls = replaceDanmakuVariants(danmaku, mat, &ok);
        if (ok)
        {
            msg.replace(mat, rpls);
            matchPos = matchPos + rpls.length();
        }
        else
        {
            if (mat.length() > 1 && mat.endsWith("%"))
                matchPos += mat.length() - 1;
            else
                matchPos += mat.length();
        }
    }

    // 根据昵称替换为uid：倒找最近的弹幕、送礼
    re = QRegularExpression("%\\(([^(%)]+?)\\)%");
    while (msg.indexOf(re, 0, &match) > -1)
    {
        QString _var = match.captured(0);
        QString text = match.captured(1);
        msg.replace(_var, snum(unameToUid(text)));
    }

    bool find = true;
    while (find)
    {
        find = false;

        // 替换JSON数据
        // 尽量支持 %.data.%{key}%.list% 这样的格式吧
        re = QRegularExpression("%\\.([^%]*?[^%\\.])%");
        matchPos = 0;
        QJsonObject json = danmaku.extraJson;
        while ((matchPos = msg.indexOf(re, matchPos, &match)) > -1)
        {
            bool ok = false;
            QString rpls = replaceDanmakuJson(json, match.captured(1), &ok);
            if (!ok)
            {
                matchPos++;
                continue;
            }
            msg.replace(match.captured(0), rpls);
            matchPos += rpls.length();
            find = true;
        }

        // 读取配置文件的变量
        re = QRegularExpression("%\\{([^(%(\\{|\\[|>))]*?)\\}%");
        while (msg.indexOf(re, 0, &match) > -1)
        {
            QString _var = match.captured(0);
            QString key = match.captured(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            QVariant var = heaps->value(key);
            msg.replace(_var, var.toString()); // 默认使用变量类型吧
            find = true;
        }

        // 进行数学计算的变量
        if (!ui->complexCalcCheck->isChecked())
            re = QRegularExpression("%\\[([\\d\\+\\-\\*/% \\(\\)]*?)\\]%"); // 纯数字+运算符+括号
        else
            re = QRegularExpression("%\\[([^(%(\\{|\\[|>))]*?)\\]%"); // 允许里面带点字母，用来扩展函数
        while (msg.indexOf(re, 0, &match) > -1)
        {
            QString _var = match.captured(0);
            QString text = match.captured(1);
            if (!ui->complexCalcCheck->isChecked())
                text = QString::number(ConditionUtil::calcIntExpression(text));
            else
                text = QString::number(CalculatorUtil::calculate(text.toStdString().c_str()));
            msg.replace(_var, text); // 默认使用变量类型吧
            find = true;
        }

        // 函数替换
        re = QRegularExpression("%>(\\w+)\\s*\\(([^(%(\\{|\\[|>))]*?)\\)%");
        matchPos = 0;
        while ((matchPos = msg.indexOf(re, matchPos, &match)) > -1)
        {
            QString rpls = replaceDynamicVariants(match.captured(1), match.captured(2), danmaku);
            msg.replace(match.captured(0), rpls);
            matchPos += rpls.length();
            find = true;
        }
    }

    // 无奈的替换
    // 一些代码特殊字符的也给替换掉
    find = true;
    while (find)
    {
        find = false;

        // 函数替换
        // 允许里面的参数出现%
        re = QRegularExpression("%>(\\w+)\\s*\\((.*?)\\)%");
        matchPos = 0;
        while ((matchPos = msg.indexOf(re, matchPos, &match)) > -1)
        {
            QString rpls = replaceDynamicVariants(match.captured(1), match.captured(2), danmaku);
            msg.replace(match.captured(0), rpls);
            matchPos += rpls.length();
            find = true;
        }
    }

    // 一些用于替换特殊字符的东西，例如想办法避免无法显示 100% 这种

    return msg;
}

QString MainWindow::replaceDanmakuVariants(const LiveDanmaku& danmaku, const QString &key, bool *ok) const
{
    *ok = true;
    QRegularExpressionMatch match;
    // 用户昵称
    if (key == "%uname%" || key == "%username%" || key =="%nickname%" || key == "%file_name%")
        return danmaku.getNickname();

    // 用户昵称
    else if (key == "%uid%")
        return snum(danmaku.getUid());

    // 本地昵称+简化
    else if (key == "%ai_name%")
    {
        QString name = getLocalNickname(danmaku.getUid());
        if (name.isEmpty())
            name = nicknameSimplify(danmaku);
        if (name.isEmpty())
            name = danmaku.getNickname();
        return name;
    }

    // 专属昵称
    else if (key == "%local_name%")
    {
        QString local = getLocalNickname(danmaku.getUid());
        if (local.isEmpty())
            local = danmaku.getNickname();
        return local;
    }

    // 昵称简化
    else if (key == "%simple_name%")
    {
        return nicknameSimplify(danmaku);
    }

    // 用户等级
    else if (key == "%level%")
        return snum(danmaku.getLevel());

    else if (key == "%text%" || key == "%file_path%")
        return toSingleLine(danmaku.getText());

    else if (key == "%url_text%")
        return QString::fromUtf8(danmaku.getText().toUtf8().toPercentEncoding());

    // 进来次数
    else if (key == "%come_count%")
    {
        if (danmaku.is(MSG_WELCOME) || danmaku.is(MSG_WELCOME_GUARD))
            return snum(danmaku.getNumber());
        else
            return snum(us->danmakuCounts->value("come/"+snum(danmaku.getUid())).toInt());
    }

    // 上次进来
    else if (key == "%come_time%")
    {
        return snum(danmaku.is(MSG_WELCOME) || danmaku.is(MSG_WELCOME_GUARD)
                                        ? danmaku.getPrevTimestamp()
                                        : us->danmakuCounts->value("comeTime/"+snum(danmaku.getUid())).toLongLong());
    }

    // 和现在的时间差
    else if (key == "%come_time_delta%")
    {
        qint64 prevTime = danmaku.is(MSG_WELCOME) || danmaku.is(MSG_WELCOME_GUARD)
                ? danmaku.getPrevTimestamp()
                : us->danmakuCounts->value("comeTime/"+snum(danmaku.getUid())).toLongLong();
        return snum(QDateTime::currentSecsSinceEpoch() - prevTime);
    }

    else if (key == "%prev_time%")
    {
        return snum(danmaku.getPrevTimestamp());
    }

    // 本次送礼金瓜子
    else if (key == "%gift_gold%")
        return snum(danmaku.isGoldCoin() ? danmaku.getTotalCoin() : 0);

    // 本次送礼银瓜子
    else if (key == "%gift_silver%")
        return snum(danmaku.isGoldCoin() ? 0 : danmaku.getTotalCoin());

    // 本次送礼金瓜子+银瓜子（应该只有一个，但直接相加了）
    else if (key == "%gift_coin%")
        return snum(danmaku.getTotalCoin());

    // 是否是金瓜子礼物
    else if (key == "%coin_gold%")
        return danmaku.isGoldCoin() ? "1" : "0";

    // 本次送礼名字
    else if (key == "%gift_name%")
        return us->giftAlias.contains(danmaku.getGiftId()) ? us->giftAlias.value(danmaku.getGiftId()) : danmaku.getGiftName();

    // 原始礼物名字
    else if (key == "%origin_gift_name%")
        return danmaku.getGiftName();

    // 本次送礼数量
    else if (key == "%gift_num%")
        return snum(danmaku.getNumber());

    else if (key == "%gift_multi_num%")
    {
        QString danwei = "个";
        QString giftName = danmaku.getGiftName();
        if (giftName.endsWith("船"))
            danwei = "艘";
        else if (giftName.endsWith("条"))
            danwei = "根";
        else if (giftName.endsWith("锦鲤"))
            danwei = "条";
        else if (giftName.endsWith("卡"))
            danwei = "张";
        else if (giftName.endsWith("灯"))
            danwei = "盏";
        else if (giftName.endsWith("阔落"))
            danwei = "瓶";
        else if (giftName.endsWith("花"))
            danwei = "朵";
        else if (giftName.endsWith("车"))
            danwei = "辆";
        else if (giftName.endsWith("情书"))
            danwei = "封";
        else if (giftName.endsWith("城"))
            danwei = "座";
        else if (giftName.endsWith("心心"))
            danwei = "颗";
        else if (giftName.endsWith("亿圆"))
            danwei = "枚";
        else if (giftName.endsWith("麦克风"))
            danwei = "支";
        return danmaku.getNumber() > 1 ? snum(danmaku.getNumber()) + danwei : "";
    }

    // 总共赠送金瓜子
    else if (key == "%total_gold%")
        return snum(us->danmakuCounts->value("gold/"+snum(danmaku.getUid())).toLongLong());

    // 总共赠送银瓜子
    else if (key == "%total_silver%")
        return snum(us->danmakuCounts->value("silver/"+snum(danmaku.getUid())).toLongLong());

    // 购买舰长
    else if (key == "%guard_buy%")
        return danmaku.is(MSG_GUARD_BUY) ? "1" : "0";

    else if (key == "%guard_buy_count%")
        return snum(us->danmakuCounts->value("guard/" + snum(danmaku.getUid()), 0).toInt());

    // 0续费，1第一次上船，2重新上船
    else if (key == "%guard_first%" || key == "%first%")
        return snum(danmaku.getFirst());

    // 特别关注
    else if (key == "%special%")
        return snum(danmaku.getSpecial());

    else if (key == "%spread%")
        return danmaku.getSpreadDesc();

    // 粉丝牌房间
    else if (key == "%anchor_roomid%" || key == "%medal_roomid%" || key == "%anchor_room_id%" || key == "%medal_room_id%")
        return danmaku.getAnchorRoomid();

    // 粉丝牌名字
    else if (key == "%medal_name%")
        return danmaku.getMedalName();

    // 粉丝牌等级
    else if (key == "%medal_level%")
        return snum(danmaku.getMedalLevel());

    // 粉丝牌主播
    else if (key == "%medal_up%")
        return danmaku.getMedalUp();

    // 房管
    else if (key == "%admin%")
        return danmaku.isAdmin() ? "1" : (!ac->upUid.isEmpty() && snum(danmaku.getUid())==ac->upUid ? "1" : "0");

    // 舰长
    else if (key == "%guard%" || key == "%guard_level%")
        return snum(danmaku.getGuard());

    // 舰长名称
    else if (key == "%guard_name%" || key == "%guard_type%")
    {
        int guard = danmaku.getGuard();
        QString name = "";
        if (guard == 1)
            name = "总督";
        else if (guard == 2)
            name = "提督";
        else if (guard == 3)
            name = "舰长";
        return name;
    }

    // 房管或舰长
    else if (key == "%admin_or_guard%")
        return (danmaku.isGuard() || danmaku.isAdmin() || (!ac->upUid.isEmpty() && snum(danmaku.getUid()) == ac->upUid)) ? "1" : "0";

    // 高能榜
    else if (key == "%online_rank%")
    {
        qint64 uid = danmaku.getUid();
        for (int i = 0; i < onlineGoldRank.size(); i++)
        {
            if (onlineGoldRank.at(i).getUid() == uid)
            {
                return snum(i + 1);
            }
        }
        return snum(0);
    }

    // 是否是姥爷
    else if (key == "%vip%")
        return danmaku.isVip() ? "1" : "0";

    // 是否是年费姥爷
    else if (key == "%svip%")
        return danmaku.isSvip() ? "1" : "0";

    // 是否是正式会员
    else if (key == "%uidentity%")
        return danmaku.isUidentity() ? "1" : "0";

    // 是否有手机验证
    else if (key == "%iphone%")
        return danmaku.isIphone() ? "1" : "0";

    // 数量
    else if (key == "%number%")
        return snum(danmaku.getNumber());

    // 昵称长度
    else if (key == "%nickname_len%")
        return snum(danmaku.getNickname().length());

    // 礼物名字长度
    else if (key == "%giftname_len%")
        return snum((us->giftAlias.contains(danmaku.getGiftId()) ? us->giftAlias.value(danmaku.getGiftId()) : danmaku.getGiftName()).length());

    // 昵称+礼物名字长度
    else if (key == "%name_sum_len%")
        return snum(danmaku.getNickname().length() + (us->giftAlias.contains(danmaku.getGiftId()) ? us->giftAlias.value(danmaku.getGiftId()) : danmaku.getGiftName()).length());

    else if (key == "%ainame_sum_len%")
    {
        QString local = getLocalNickname(danmaku.getUid());
        if (local.isEmpty())
            local = nicknameSimplify(danmaku);
        if (local.isEmpty())
            local = danmaku.getNickname();
        return snum(local.length() + (us->giftAlias.contains(danmaku.getGiftId()) ? us->giftAlias.value(danmaku.getGiftId()) : danmaku.getGiftName()).length());
    }

    // 是否新关注
    else if (key == "%new_attention%")
    {
        bool isInFans = false;
        qint64 uid = danmaku.getUid();
        foreach (FanBean fan, fansList)
            if (fan.mid == uid)
            {
                isInFans = true;
                break;
            }
        return isInFans ? "1" : "0";
    }

    // 是否是对面串门
    else if (key == "%pk_opposite%")
        return danmaku.isOpposite() ? "1" : "0";

    // 是否是己方串门回来
    else if (key == "%pk_view_return%")
        return danmaku.isViewReturn() ? "1" : "0";

    // 本次进来人次
    else if (key == "%today_come%")
        return snum(dailyCome);

    // 新人发言数量
    else if (key == "%today_newbie_msg%")
        return snum(dailyNewbieMsg);

    // 今天弹幕总数
    else if (key == "%today_danmaku%")
        return snum(dailyDanmaku);

    // 今天新增关注
    else if (key == "%today_fans%")
        return snum(dailyNewFans);

    // 大航海人数
    else if (key == "%guard_count%")
        return snum(guardInfos.size());

    // 当前粉丝数量
    else if (key == "%fans_count%")
        return snum(ac->currentFans);
    else if (key == "%fans_club%")
        return snum(ac->currentFansClub);

    // 今天金瓜子总数
    else if (key == "%today_gold%")
        return snum(dailyGiftGold);

    // 今天银瓜子总数
    else if (key == "%today_silver%")
        return snum(dailyGiftSilver);

    // 今天是否有新舰长
    else if (key == "%today_guard%")
        return snum(dailyGuard);

    // 今日最高人气
    else if (key == "%today_max_ppl%")
        return snum(dailyMaxPopul);

    // 当前人气
    else if (key == "%popularity%")
        return snum(ac->currentPopul);

    // 当前时间
    else if (key == "%time_hour%")
        return snum(QTime::currentTime().hour());
    else if (key == "%time_minute%")
        return snum(QTime::currentTime().minute());
    else if (key == "%time_second%")
        return snum(QTime::currentTime().second());
    else if (key == "%time_day%")
        return snum(QDate::currentDate().day());
    else if (key == "%time_month%")
        return snum(QDate::currentDate().month());
    else if (key == "%time_year%")
        return snum(QDate::currentDate().year());
    else if (key == "%time_day_week%")
        return snum(QDate::currentDate().dayOfWeek());
    else if (key == "%time_day_year%")
        return snum(QDate::currentDate().dayOfYear());
    else if (key == "%timestamp%")
        return snum(QDateTime::currentSecsSinceEpoch());
    else if (key == "%timestamp13%")
        return snum(QDateTime::currentMSecsSinceEpoch());

    // 大乱斗
    else if (key == "%pking%")
        return snum(pking ? 1 : 0);
    else if (key == "%pk_video%")
        return snum(pkVideo ? 1 : 0);
    else if (key == "%pk_room_id%")
        return pkRoomId;
    else if (key == "%pk_uid%")
        return pkUid;
    else if (key == "%pk_uname%")
        return pkUname;
    else if (key == "%pk_count%")
        return snum(!pkRoomId.isEmpty() ? us->danmakuCounts->value("pk/" + pkRoomId, 0).toInt() : 0);
    else if (key == "%pk_touta_prob%")
    {
        int prob = 0;
        if (pking && !pkRoomId.isEmpty())
        {
            int totalCount = us->danmakuCounts->value("pk/" + pkRoomId, 0).toInt() - 1;
            int toutaCount = us->danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
            if (totalCount > 1)
                prob = toutaCount * 100 / totalCount;
        }
        return snum(prob);
    }

    else if (key == "%pk_my_votes%")
        return snum(myVotes);
    else if (key == "%pk_match_votes%")
        return snum(matchVotes);
    else if (key == "%pk_ending%")
        return snum(pkEnding ? 1 : 0);
    else if (key == "%pk_trans_gold%")
        return snum(goldTransPk);
    else if (key == "%pk_max_gold%")
        return snum(pkMaxGold);

    else if (key == "%pk_id%")
        return snum(pkId);

    // 房间属性
    else if (key == "%living%")
        return snum(ac->liveStatus);
    else if (key == "%room_id%")
        return ac->roomId;
    else if (key == "%room_name%")
        return ac->roomTitle;
    else if (key == "%up_name%" || key == "%up_uname%")
        return ac->upName;
    else if (key == "%up_uid%")
        return ac->upUid;
    else if (key == "%my_uid%")
        return ac->cookieUid;
    else if (key == "%my_uname%")
        return ac->cookieUname;
    else if (key == "%area_id%")
        return ac->areaId;
    else if (key == "%area_name%")
        return ac->areaName;
    else if (key == "%parent_area_id%")
        return ac->parentAreaId;
    else if (key == "%parent_area_name%")
        return ac->parentAreaName;

    // 是主播
    else if (key == "%is_up%")
        return danmaku.getUid() == ac->upUid.toLongLong() ? "1" : "0";
    // 是机器人
    else if (key == "%is_me%")
        return danmaku.getUid() == ac->cookieUid.toLongLong() ? "1" : "0";
    // 戴房间勋章
    else if (key == "%is_room_medal%")
        return danmaku.getAnchorRoomid() == ac->roomId ? "1" : "0";

    // 本地设置
    // 特别关心
    else if (key == "%care%")
        return us->careUsers.contains(danmaku.getUid()) ? "1" : "0";
    // 强提醒
    else if (key == "%strong_notify%")
        return us->strongNotifyUsers.contains(danmaku.getUid()) ? "1" : "0";
    // 是否被禁言
    else if (key == "%blocked%")
        return us->userBlockIds.contains(danmaku.getUid()) ? "1" : "0";
    // 不自动欢迎
    else if (key == "%not_welcome%")
        return us->notWelcomeUsers.contains(danmaku.getUid()) ? "1" : "0";
    // 不自动欢迎
    else if (key == "%not_reply%")
        return us->notReplyUsers.contains(danmaku.getUid()) ? "1" : "0";

    // 弹幕人气
    else if (key == "%danmu_popularity%")
        return snum(danmuPopulValue);

    // 游戏用户
    else if (key == "%in_game_users%")
        return gameUsers[0].contains(danmaku.getUid()) ? "1" : "0";
    else if (key == "%in_game_numbers%")
        return gameNumberLists[0].contains(danmaku.getUid()) ? "1" : "0";
    else if (key == "%in_game_texts%")
        return gameTextLists[0].contains(danmaku.getText()) ? "1" : "0";

    // 程序文件、路径
    else if (key == "%app_name%")
        return rt->appFileName;

    else if (key == "%app_path%")
        return QDir(rt->dataPath).absolutePath();

    else if (key == "%www_path%")
        return wwwDir.absolutePath();

    else if (key == "%server_domain%")
        return serverDomain.contains("://") ? serverDomain : "http://" + serverDomain;

    else if (key == "%server_port%")
        return snum(serverPort);

    else if (key == "%server_url%")
        return serverDomain + ":" + snum(serverPort);

    // cookie
    else if (key == "%csrf%")
        return ac->csrf_token;

    // 工作状态
    else if (key == "%working%")
        return isWorking() ? "1" : "0";

    // 用户备注
    else if (key == "%umark%")
        return us->userMarks->value("base/" + snum(danmaku.getUid()), "").toString();

    // 对面直播间也在用神奇弹幕
    else if (key == "%pk_magical_room%")
        return !pkRoomId.isEmpty() && magicalRooms.contains(pkRoomId) ? "1" : "0";

    // 正在播放的音乐
    else if (key == "%playing_song%")
    {
        QString name = "";
        if (musicWindow)
        {
            Song song = musicWindow->getPlayingSong();
            if (song.isValid())
                name = song.name;
        }
        return name;
    }
    // 点歌的用户
    else if (key == "%song_order_uname%")
    {
        QString name = "";
        if (musicWindow)
        {
            Song song = musicWindow->getPlayingSong();
            if (song.isValid())
                name = song.addBy;
        }
        return name;
    }
    // 点歌队列数量
    else if (key == "%order_song_count%")
    {
        QString text = "0";
        if (musicWindow)
        {
            text = snum(musicWindow->getOrderSongs().size());
        }
        return text;
    }
    else if (key == "%random100%")
    {
        return snum(qrand() % 100 + 1);
    }
    else if (key.indexOf(QRegularExpression("^%cd(\\d{1,2})%$"), 0, &match) > -1)
    {
        int ch = match.captured(1).toInt();
        qint64 time = msgCds[ch];
        qint64 curr = QDateTime::currentMSecsSinceEpoch();
        qint64 delta = curr - time;
        return snum(delta);
    }
    else if (key.indexOf(QRegularExpression("^%wait(\\d{1,2})%$"), 0, &match) > -1)
    {
        int ch = match.captured(1).toInt();
        return snum(msgWaits[ch]);
    }
    else if (key == "%local_mode%")
    {
        return localDebug ? "1" : "0";
    }
    else if (key == "%repeat_10%")
    {
        return hasSimilarOldDanmaku(danmaku.getText()) ? "1" : "0";
    }
    else if (key == "%playing_tts%")
    {
        switch (voicePlatform) {
        case VoiceLocal:
    #if defined(ENABLE_TEXTTOSPEECH)
            return (tts && tts->state() == QTextToSpeech::Speaking) ? "1" : "0";
    #else
            return "0";
    #endif
        case VoiceXfy:
            return (xfyTTS && xfyTTS->isPlaying()) ? "1" : "0";
        case VoiceMS:
            return (msTTS && msTTS->isPlaying()) ? "1" : "0";
        case VoiceCustom:
            return (ttsDownloading || (ttsPlayer && ttsPlayer->state() == QMediaPlayer::State::PlayingState)) ? "1" : "0";
        }
    }
    else if (key == "%mouse_x%")
    {
        return snum(QCursor().pos().x());
    }
    else if (key == "%mouse_y%")
    {
        return snum(QCursor().pos().y());
    }
    else
    {
        *ok = false;
        return "";
    }
}

/**
 * 额外数据（JSON）替换
 */
QString MainWindow::replaceDanmakuJson(const QJsonObject &json, const QString& key_seq, bool* ok) const
{
    QStringList keyTree = key_seq.split(".");
    if (keyTree.size() == 0)
        return "";

    QJsonValue obj = json;
    while (keyTree.size())
    {
        QString key = keyTree.takeFirst();
        bool canIgnore = false;
        if (key.endsWith("?"))
        {
            canIgnore = true;
            key = key.left(key.length() - 1);
        }

        if (key.isEmpty()) // 居然是空字符串，是不是有问题
        {
            if (canIgnore)
            {
                obj = QJsonValue();
                break;
            }
            else
            {
                qWarning() << "解析JSON的键是空的：" << key_seq;
                return "";
            }
        }
        else if (obj.isObject())
        {
            if (!obj.toObject().contains(key)) // 不包含这个键
            {
                // qWarning() << "不存在的键：" << key << "    keys:" << key_seq;
                if (canIgnore)
                {
                    obj = QJsonValue();
                    break;
                }
                else
                {
                    // 可能是等待其他的变量，所以先留下
                    return "";
                }
            }
            obj = obj.toObject().value(key);
        }
        else if (obj.isArray())
        {
            int index = key.toInt();
            QJsonArray array = obj.toArray();
            if (index >= 0 && index < array.size())
            {
                obj = array.at(index);
            }
            else
            {
                if (canIgnore)
                {
                    obj = QJsonValue();
                    break;
                }
                else
                {
                    qWarning() << "错误的数组索引：" << index << "当前总数量：" << array.size() << "  " << key_seq;
                    return "";
                }
            }
        }
        else
        {
            qWarning() << "未知的JSON类型：" << key_seq;
            obj = QJsonValue();
            break;
        }
    }

    *ok = true;

    if (obj.isNull() || obj.isUndefined())
        return "";
    if (obj.isString())
        return toSingleLine(obj.toString());
    if (obj.isBool())
        return obj.toBool(false) ? "1" : "0";
    if (obj.isDouble())
    {
        double val = obj.toDouble();
        if (qAbs(val - qint64(val)) < 1e-6) // 是整数类型的
            return QString::number(qint64(val));
        return QString::number(val);
    }
    if (obj.isObject() || obj.isArray()) // 不支持转换的类型
        return "";
    return "";
}

/**
 * 函数替换
 */
QString MainWindow::replaceDynamicVariants(const QString &funcName, const QString &args, const LiveDanmaku& danmaku)
{
    QRegularExpressionMatch match;
    QStringList argList = args.split(QRegExp("\\s*,\\s*"));
    auto errorArg = [=](QString tip){
        localNotify("函数%>"+funcName+"()%参数错误: " + tip);
        return "";
    };

    // 替换时间
    if (funcName == "simpleName")
    {
        LiveDanmaku ld = danmaku;
        ld.setNickname(args);
        return nicknameSimplify(ld);
    }
    else if (funcName == "simpleNum")
    {
        return numberSimplify(args.toInt());
    }
    else if (funcName == "time")
    {
        return QDateTime::currentDateTime().toString(args);
    }
    else if (funcName == "unameToUid")
    {
        qint64 uid = unameToUid(args);
        return snum(uid);
    }
    else if (funcName == "inGameUsers")
    {
        int ch = 0;
        qint64 id = 0;
        if (argList.size() == 1)
            id = argList.first().toLongLong();
        else if (argList.size() >= 2)
        {
            ch = argList.at(0).toInt();
            id = argList.at(1).toLongLong();
        }
        if (ch < 0 || ch >= CHANNEL_COUNT)
            ch = 0;
        return gameUsers[ch].contains(id) ? "1" : "0";
    }
    else if (funcName == "inGameNumbers")
    {
        int ch = 0;
        qint64 id = 0;
        if (argList.size() == 1)
            id = argList.first().toLongLong();
        else if (argList.size() >= 2)
        {
            ch = argList.at(0).toInt();
            id = argList.at(1).toLongLong();
        }
        if (ch < 0 || ch >= CHANNEL_COUNT)
            ch = 0;
        return gameNumberLists[ch].contains(id) ? "1" : "0";
    }
    else if (funcName == "inGameTexts")
    {
        int ch = 0;
        QString text;
        if (argList.size() == 1)
            text = argList.first();
        else if (argList.size() >= 2)
        {
            ch = argList.at(0).toInt();
            text = argList.at(1);
        }
        if (ch < 0 || ch >= CHANNEL_COUNT)
            ch = 0;
        return gameTextLists[ch].contains(text) ? "1" : "0";
    }
    else if (funcName == "strlen")
    {
        return snum(args.size());
    }
    else if (funcName == "trim")
    {
        return args.trimmed();
    }
    else if (funcName == "substr")
    {
        if (argList.size() < 2)
            return errorArg("字符串, 起始位置, 长度");

        QString text = argList.at(0);
        int left = 0, len = text.length();
        if (argList.size() >= 2)
            left = argList.at(1).toInt();
        if (argList.size() >= 3)
            len = argList.at(2).toInt();
        if (left < 0)
            left = 0;
        else if (left > text.length())
            left = text.length();
        return text.mid(left, len);
    }
    else if (funcName == "replace")
    {
        if (argList.size() < 3)
            return errorArg("字符串, 原文本, 新文本");

        QString text = argList.at(0);
        return text.replace(argList.at(1), argList.at(2));
    }
    else if (funcName == "replaceReg" || funcName == "regReplace")
    {
        if (argList.size() < 3)
            return errorArg("字符串, 原正则, 新文本");

        QString text = argList.at(0);
        return text.replace(QRegularExpression(argList.at(1)), argList.at(2));
    }
    else if (funcName == "reg")
    {
        if (argList.size() < 2)
            return errorArg("字符串, 正则表达式");
        int index = args.lastIndexOf(",");
        if (index == -1) // 不应该没找到的
            return "";
        QString full = args.left(index);
        QString reg = args.right(args.length() - index - 1).trimmed();

        QRegularExpressionMatch match;
        if (full.indexOf(QRegularExpression(reg), 0, &match) == -1)
            return "";
        return match.captured(0);
    }
    else if (funcName == "inputText")
    {
        QString label = argList.size() ? argList.first() : "";
        QString def = argList.size() >= 2 ? argList.at(1) : "";
        QString rst = QInputDialog::getText(this, QApplication::applicationName(), label, QLineEdit::Normal, def);
        return rst;
    }
    else if (funcName == "getValue")
    {
        if (argList.size() < 1 || argList.first().trimmed().isEmpty())
            return errorArg("键, 默认值");
        QString key = argList.at(0);
        QString def = argList.size() >= 2 ? argList.at(1) : "";
        if (!key.contains("/"))
            key = "heaps/" + key;
        return heaps->value(key, def).toString();
    }
    else if (funcName == "random")
    {
        if (argList.size() < 1 || argList.first().trimmed().isEmpty())
            return errorArg("最小值，最大值");
        int min = 0, max = 100;
        if (argList.size() == 1)
            max = argList.at(0).toInt();
        else
        {
            min = argList.at(0).toInt();
            max = argList.at(1).toInt();
        }
        if (max < min)
        {
            int t = min;
            min = max;
            max = t;
        }
        return snum(qrand() % (max-min+1) + min);
    }
    else if (funcName == "randomArray")
    {
        if (!argList.size())
            return "";
        int r = qrand() % argList.size();
        return argList.at(r);
    }
    else if (funcName == "filterReject")
    {
        if (argList.size() < 1 || argList.first().trimmed().isEmpty())
            return errorArg("最小值，最大值");
        QString filterName = args;
        return isFilterRejected(filterName, danmaku) ? "1" : "0";
    }
    else if (funcName == "inFilterList") // 匹配过滤器的内容，空白符分隔
    {
        if (argList.size() < 2)
            return errorArg("过滤器名, 全部内容");
        int index = args.indexOf(",");
        if (index == -1) // 不应该没找到的
            return "";

        QString filterName = args.left(index);
        QString content = args.right(args.length() - index - 1).trimmed();
        qInfo() << ">inFilterList:" << filterName << content;
        for (int row = 0; row < ui->eventListWidget->count(); row++)
        {
            auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(row));
            auto tw = static_cast<EventWidget*>(widget);
            if (tw->eventEdit->text() != filterName || !tw->check->isChecked())
                continue;

            QStringList sl = tw->body().split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
            foreach (QString s, sl)
            {
                if (content.contains(s))
                {
                    qInfo() << "keys:" << sl;
                    return "1";
                }
            }
        }
        return "0";
    }
    else if (funcName == "inFilterMatch") // 匹配过滤器的正则
    {
        if (argList.size() < 2)
            return errorArg("过滤器名, 全部内容");
        int index = args.indexOf(",");
        if (index == -1) // 不应该没找到的
            return "";
        QString filterName = args.left(index);
        QString content = args.right(args.length() - index - 1).trimmed();
        qInfo() << ">inFilterMatch:" << filterName << content;
        for (int row = 0; row < ui->eventListWidget->count(); row++)
        {
            auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(row));
            auto tw = static_cast<EventWidget*>(widget);
            if (tw->eventEdit->text() != filterName || !tw->check->isChecked())
                continue;

            QStringList sl = tw->body().split("\n", QString::SkipEmptyParts);
            foreach (QString s, sl)
            {
                if (content.indexOf(QRegularExpression(s)) > -1)
                {
                    qInfo() << "regs:" << sl;
                    return "1";
                }
            }
        }
        return "0";
    }
    else if (funcName == "abs")
    {
        QString val = args.trimmed();
        if (val.startsWith("-"))
            val.remove(0, 1);
        return val;
    }
    else if (funcName == "log2")
    {
        if (argList.size() < 1)
        {
            return errorArg("数值");
        }

        double a = argList.at(0).toDouble();
        return QString::number(int(log2(a)));
    }
    else if (funcName == "log10")
    {
        if (argList.size() < 1)
        {
            return errorArg("数值");
        }

        double a = argList.at(0).toDouble();
        return QString::number(int(log10(a)));
    }
    else if (funcName == "pow")
    {
        if (argList.size() < 2)
        {
            return errorArg("底数, 指数");
        }

        double a = argList.at(0).toDouble();
        double b = argList.at(1).toDouble();
        return QString::number(int(pow(a, b)));
    }
    else if (funcName == "pow2")
    {
        if (argList.size() < 1)
        {
            return errorArg("底数");
        }

        double a = argList.at(0).toDouble();
        return QString::number(int(a * a));
    }
    else if (funcName == "cd")
    {
        int ch = 0;
        if (argList.size() > 0)
            ch = argList.at(0).toInt();
        qint64 time = msgCds[ch];
        qint64 curr = QDateTime::currentMSecsSinceEpoch();
        qint64 delta = curr - time;
        return snum(delta);
    }
    else if (funcName == "wait")
    {
        int ch = 0;
        if (argList.size() > 0)
            ch = argList.at(0).toInt();
        return snum(msgWaits[ch]);
    }
    else if (funcName == "pasteText")
    {
        return QApplication::clipboard()->text();
    }
    else if (funcName == "getScreenPositionColor") // 获取屏幕上某个点的颜色
    {
        if (argList.size() < 3)
        {
            return errorArg("屏幕ID, 横坐标, 纵坐标");
        }
        int wid = argList.at(0).toInt(); // 屏幕ID
        int x = argList.at(1).toInt(); // 横坐标
        int y = argList.at(2).toInt(); // 纵坐标

        auto screens = QGuiApplication::screens();
        if (wid < 0 || wid >= screens.size())
        {
            showError("getScreenPositionColor", "没有该屏幕：" + QString::number(wid)
                      + "/" + QString::number(screens.size()));
            return "";
        }
        QScreen *screen = screens.at(wid);
        QPixmap pixmap = screen->grabWindow(wid, x, y, 1, 1);
        QColor color = pixmap.toImage().pixelColor(0, 0);
        return QVariant(color).toString();
    }
    else if (funcName == "getWindowPositionColor") // 获取屏幕上某个点的颜色
    {
        QString name = argList.at(0); // 窗口名字
        bool isId = false;
        int useId = name.toInt(&isId);
        int x = argList.at(1).toInt(); // 横坐标
        int y = argList.at(2).toInt(); // 纵坐标

        qint64 wid = 0;
        // 获取窗口句柄
#ifdef Q_OS_WIN
        {
            HWND pWnd = first_window(EXCLUDE_MINIMIZED); // 得到第一个窗口句柄
            while (pWnd)
            {
                QString title = get_window_title(pWnd);
                QString clss = get_window_class(pWnd);

                if (!title.isEmpty())
                {
                    if (title.contains(name) || clss.contains(name)
                            || (isId && (useId == reinterpret_cast<qint64>(pWnd))) )
                    {
                        wid = reinterpret_cast<qint64>(pWnd);
                        break;
                    }
                }

                pWnd = next_window(pWnd, EXCLUDE_MINIMIZED); // 得到下一个窗口句柄
            }
        }
        if (wid == 0)
        {
            showError("getWindowPositionColor", "无法查询到窗口句柄");
            return "";
        }
#endif

        QScreen *screen = QGuiApplication::primaryScreen();
        QPixmap pixmap = screen->grabWindow(wid, x, y, 1, 1);
        QColor color = pixmap.toImage().pixelColor(0, 0);
        return QVariant(color).toString();
    }
    else if (funcName == "execReplyResult" || funcName == "getReplyExecutionResult")
    {
        if (argList.size() < 1 || args.isEmpty())
        {
            return errorArg("回复内容");
        }
        QString key = args;
        return getReplyExecutionResult(key, danmaku);
    }
    else if (funcName == "execEventResult" || funcName == "getEventExecutionResult")
    {
        if (argList.size() < 1 || args.isEmpty())
        {
            return errorArg("事件名字");
        }
        QString key = args;
        return getEventExecutionResult(key, danmaku);
    }
    else if (funcName == "readTextFile")
    {
        if (argList.size() < 1 || args.isEmpty())
            return errorArg("文件路径");
        QString fileName = toFilePath(argList.at(0));
        QString content = readTextFileAutoCodec(fileName);
        return toSingleLine(content);
    }
    else if (funcName == "fileExists" || funcName == "isFileExist")
    {
        if (argList.size() < 1 || args.isEmpty())
            return errorArg("文件路径");
        QString fileName = toFilePath(argList.at(0));
        return isFileExist(fileName) ? "1" : "0";
    }
    else if (funcName == "getTextFileLine")
    {
        if (argList.size() < 1 || args.isEmpty())
            return errorArg("文件路径");
        QString fileName = toFilePath(argList.at(0));
        QString content = readTextFileAutoCodec(fileName);
        QStringList sl = content.split("\n");
        int line = argList.at(1).toInt();
        line--;
        if (line < 0 || line >= sl.size())
        {
            showError("getTextFileLine", "错误的行数：" + snum(line+1) + "不在1~" + snum(sl.size()) + "之间");
            return "";
        }
        return sl.at(line);
    }
    else if (funcName == "getTextFileLineCount")
    {
        if (argList.size() < 1 || args.isEmpty())
            return errorArg("文件路径");
        QString fileName = toFilePath(argList.at(0));
        QString content = readTextFileAutoCodec(fileName);
        return snum(content.split("\n").size());
    }
    else if (funcName == "urlEncode")
    {
        if (argList.size() < 1 || args.isEmpty())
            return errorArg("字符串");
        return QByteArray(toMultiLine(args).toUtf8()).toPercentEncoding();
    }
    else if (funcName == "urlDecode")
    {
        if (argList.size() < 1 || args.isEmpty())
            return errorArg("字符串");
        return toSingleLine(QByteArray::fromPercentEncoding(args.toUtf8()));
    }

    return "";
}

/**
 * 处理条件变量
 * [exp1, exp2]...
 * 要根据时间戳、字符串
 * @return 如果返回空字符串，则不符合；否则返回去掉表达式后的正文
 */
QString MainWindow::processMsgHeaderConditions(QString msg) const
{
    QString condRe;
    if (msg.contains(QRegularExpression("^\\s*\\[\\[\\[.*\\]\\]\\]")))
        condRe = "^\\s*\\[\\[\\[(.*?)\\]\\]\\]\\s*";
    else if (msg.contains(QRegularExpression("^\\s*\\[\\[.*\\]\\]"))) // [[%text% ~ "[\\u4e00-\\u9fa5]+[\\w]{3}[\\u4e00-\\u9fa5]+"]]
        condRe = "^\\s*\\[\\[(.*?)\\]\\]\\s*";
    else
        condRe = "^\\s*\\[(.*?)\\]\\s*";
    QRegularExpression re(condRe);
    if (!re.isValid())
        showError("无效的条件表达式", condRe);
    QRegularExpressionMatch match;
    if (msg.indexOf(re, 0, &match) == -1) // 没有检测到条件表达式，直接返回
        return msg;

    CALC_DEB << "条件表达式：" << match.capturedTexts();
    QString totalExp = match.capturedTexts().first(); // 整个表达式，带括号
    QString exprs = match.capturedTexts().at(1);

    if (!ConditionUtil::judgeCondition(exprs))
        return "";
    return msg.right(msg.length() - totalExp.length());
}

bool MainWindow::isFilterRejected(QString filterName, const LiveDanmaku &danmaku)
{
    if (!enableFilter)
        return false;

    // 查找所有事件，查看有没有对应的过滤器
    bool reject = false;
    for (int row = 0; row < ui->eventListWidget->count(); row++)
    {
        auto rowItem = ui->eventListWidget->item(row);
        auto widget = ui->eventListWidget->itemWidget(rowItem);
        if (!widget)
            continue;
        auto eventWidget = static_cast<EventWidget*>(widget);
        if (eventWidget->isEnabled() && eventWidget->title() == filterName)
        {
            QString filterText = eventWidget->body();
            // 判断事件
            if (!processFilter(filterText, danmaku))
                reject = true;
        }
    }

    return reject;
}

/**
 * 判断是否通过（没有reject）
 */
bool MainWindow::processFilter(QString filterText, const LiveDanmaku &danmaku)
{
    if (filterText.isEmpty())
        return false;

    // 获取符合的所有结果
    QStringList msgs = getEditConditionStringList(filterText, danmaku);
    if (!msgs.size())
        return true;

    // 如果有多个，随机取一个
    int r = qrand() % msgs.size();
    QString s = msgs.at(r);

    bool reject = s.contains(QRegularExpression(">\\s*reject\\s*(\\s*)"));
    if (reject)
        qInfo() << "已过滤:" << filterText;

    if (reject && !s.contains("\\n")) // 拒绝，且不需要其他操作，直接返回
    {
        return false;
    }

    if (!s.trimmed().isEmpty()) // 可能还有其他的操作
    {
        sendAutoMsg(s, danmaku);
    }

    return !reject;
}

/// 替换 unicode
/// 即 \u4000 这种
void MainWindow::translateUnicode(QString &s) const
{
    s.replace("\\中文", "\u4e00-\u9fa5");
}

qint64 MainWindow::unameToUid(QString text)
{
    // 查找弹幕和送礼
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;

        QString nick = danmaku.getNickname();
        if (nick.contains(text))
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UNAME", danmaku, true);
            return danmaku.getUid();
        }
    }

    // 查找专属昵称
    QSet<qint64> hadMatches;
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;
        qint64 uid = danmaku.getUid();
        if (hadMatches.contains(uid) || !us->localNicknames.contains(uid))
            continue;
        QString nick = us->localNicknames.value(uid);
        if (nick.contains(text))
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UNAME", danmaku, true);
            return danmaku.getUid();
        }
        hadMatches.insert(uid);
    }

    localNotify("[未找到用户：" + text + "]");
    triggerCmdEvent("NOT_FIND_USER_BY_UNAME", LiveDanmaku(text), true);
    return 0;
}

QString MainWindow::uidToName(qint64 uid)
{
    // 查找弹幕和送礼
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;

        if (danmaku.getUid() == uid)
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UID", danmaku, true);
            return danmaku.getNickname();
        }
    }

    // 查找专属昵称
    QSet<qint64> hadMatches;
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;
        if (danmaku.getUid() == uid)
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UID", danmaku, true);
            return danmaku.getNickname();
        }
        hadMatches.insert(uid);
    }

    localNotify("[未找到用户：" + snum(uid) + "]");
    triggerCmdEvent("NOT_FIND_USER_BY_UID", LiveDanmaku(snum(uid)), true);
    return "";
}

/**
 * 一个智能的用户昵称转简单称呼
 */
QString MainWindow::nicknameSimplify(const LiveDanmaku &danmaku) const
{
    QString name = danmaku.getNickname();
    QString simp = name;

    // 没有取名字的，就不需要欢迎了
    /*QRegularExpression defaultRe("^([bB]ili_\\d+|\\d+_[bB]ili)$");
    if (simp.indexOf(defaultRe) > -1)
    {
        return "";
    }*/

    // 去掉前缀后缀
    QStringList special{"~", "丶", "°", "゛", "-", "_", "ヽ", "丿", "'"};
    QStringList starts{"我叫", "叫我", "我是", "我就是", "可是", "一只", "一个", "是个", "是", "原来", "但是", "但",
                       "在下", "做", "隔壁", "的", "寻找", "为什么"};
    QStringList ends{"啊", "呢", "呀", "哦", "呐", "巨凶", "吧", "呦", "诶", "哦", "噢", "吖", "Official"};
    starts += special;
    ends += special;
    for (int i = 0; i < starts.size(); i++)
    {
        QString start = starts.at(i);
        if (simp.startsWith(start))
        {
            simp = simp.remove(0, start.length());
            i = 0; // 从头开始
        }
    }
    for (int i = 0; i < ends.size(); i++)
    {
        QString end = ends.at(i);
        if (simp.endsWith(end))
        {
            simp = simp.remove(simp.length() - end.length(), end.length());
            i = 0; // 从头开始
        }
    }

    // 默认名字
    QRegularExpression defRe("(\\d+)_[Bb]ili");
    QRegularExpressionMatch match;
    if (simp.indexOf(defRe, 0, &match) > -1)
    {
        simp = match.capturedTexts().at(1);
    }

    // 去掉首尾数字
    QRegularExpression snumRe("^\\d+(\\D+)\\d*$");
    if (simp.indexOf(snumRe, 0, &match) > -1
            && match.captured(1).indexOf(QRegExp("^[的是]")) == -1)
    {
        simp = match.capturedTexts().at(1);
    }
    snumRe = QRegularExpression("^(\\D+)\\d+$");
    if (simp.indexOf(snumRe, 0, &match) > -1
            && match.captured(1) != "bili_"
            && match.captured(1).indexOf(QRegExp("^[的是]")) == -1)
    {
        simp = match.capturedTexts().at(1);
    }

    // 一大串 中文enen
    // 注：日语正则 [\u0800-\u4e00] ，实测“一”也会算在里面……？
    QRegularExpression ceRe("([\u4e00-\u9fa5]{2,})([-_\\w\\d_\u0800-\u4dff]+)$");
    if (simp.indexOf(ceRe, 0, &match) > -1 && match.capturedTexts().at(1).length()*3 >= match.capturedTexts().at(2).length())
    {
        QString tmp = match.capturedTexts().at(1);
        if (!QString("的之の是叫有为奶在去着最很").contains(tmp.right(1)))
        {
            simp = tmp;
        }
    }
    // enen中文
    ceRe = QRegularExpression("^([-\\w\\d_\u0800-\u4dff]+)([\u4e00-\u9fa5]{2,})$");
    if (simp.indexOf(ceRe, 0, &match) > -1 && match.capturedTexts().at(1).length() <= match.capturedTexts().at(2).length()*3)
    {
        QString tmp = match.capturedTexts().at(2);
        if (!QString("的之の是叫有为奶在去着最").contains(tmp.right(1)))
        {
            simp = tmp;
        }
    }

    // xxx的xxx
    QRegularExpression deRe("^(.{2,})[的の]([\\w\\d_\\-\u4e00-\u9fa5]{2,})$");
    if (simp.indexOf(deRe, 0, &match) > -1 && match.capturedTexts().at(1).length() <= match.capturedTexts().at(2).length()*2)
    {
        QRegularExpression blankL("(我$)"), blankR("(名字|^确|最)");
        if (match.captured(1).indexOf(blankL) == -1
                && match.captured(2).indexOf(blankR) == -1) // 不包含黑名单
            simp = match.capturedTexts().at(2);
    }

    // 固定格式1
    QStringList extraExp{"^(.{2,}?)[-_]\\w{2,}$",
                        "^这个(.+)不太.+$", "^(.{3,})今天.+$", "最.+的(.{2,})$",
                         "^.*还.+就(.{2})$",
                         "^(.{2,})(.)不\\2.*",
                         "我不是(.{2,})$",
                         "^(.{2,}?)(永不|有点|才是|敲|很|能有|都|想|要|需|真的|从不|才不|不|跟你|和你|这\
|超好|是我|来自|是只|今天|我是).+",
                         "^(.{2,})-(.{2,})$",
                         "^(.{2,}?)[最很超特别是没不想好可以能要]*有.+$",
                         "^.*?(?:是个|看我)(.{2,})$"
                         "^(不做|只因)(.{2,})$",
                        "^x+(.{2,}x+)$",
                        "^(.{2,})(.+)\\1$",
                        "^不要(.{2,})"};
    for (int i = 0; i < extraExp.size(); i++)
    {
        QRegularExpression re(extraExp.at(i));
        if (simp.indexOf(re, 0, &match) > -1)
        {
            QString partName = match.capturedTexts().at(1);
            if (partName.contains(QRegularExpression("我|你|很|想|是|不")))
                continue;
            simp = partName;
            break;
        }
    }

    // 特殊字符
    simp = simp.replace(QRegularExpression("_|丨|丶|灬|ミ|丷|I"), "");

    // 身份后缀：xxx哥哥
    QRegularExpression gegeRe("^(.{2,}?)(大|小|老)?(公|婆|鸽鸽|哥哥|爸爸|爷爷|奶奶|妈妈|朋友|盆友|魔王|可爱|参上|大大|大人)$");
    if (simp.indexOf(gegeRe, 0, &match) > -1)
    {
        QString tmp = match.capturedTexts().at(1);
        simp = tmp;
    }

    // 重复词：AAAA
    QRegularExpression aaRe("(.)\\1{2,}");
    if (simp.indexOf(aaRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1%1").arg(ch));
    }

    // ABAB
    QRegularExpression abRe = QRegularExpression("(.{2,})\\1{1,}");
    if (simp.indexOf(abRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1").arg(ch));
    }

    // Name1Name2
    QRegularExpression sunameRe = QRegularExpression("^[A-Z]*?([A-Z][a-z0-9]+)[-_A-Z0-9]([\\w_\\-])*$");
    if (simp.indexOf(sunameRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1").arg(ch));
    }

    // 一长串数字
    QRegularExpression numRe("(\\d{3})\\d{3,}");
    if (simp.indexOf(numRe, 0, &match) > -1)
    {
        simp = simp.replace(match.captured(0), match.captured(1) + "…");
    }

    // 一长串英文
    QRegularExpression wRe("(\\w{5})\\w{3,}");
    if (simp.indexOf(wRe, 0, &match) > -1)
    {
        simp = simp.replace(match.captured(0), match.captured(1) + "…");
    }

    // 固定搭配替换
    QHash<QString, QString>repl;
    repl.insert("奈子", "奈X");
    repl.insert("走召", "超");
    repl.insert("リ巾", "帅");
    repl.insert("白勺", "的");
    for (auto it = repl.begin(); it != repl.end(); it++)
    {
        simp = simp.replace(it.key(), it.value());
    }

    if (simp.isEmpty())
        return name;
    return simp;
}

QString MainWindow::numberSimplify(int number) const
{
    if (number < 10000)
        return QString::number(number);
    number = (number + 5000) / 10000;
    return QString::number(number) + "万";
}

QString MainWindow::msgToShort(QString msg) const
{
    if (msg.startsWith(">") || msg.length() <= danmuLongest)
        return msg;
    if (msg.contains(" "))
    {
        msg = msg.replace(" ", "");
        if (msg.length() <= danmuLongest)
            return msg;
    }
    if (msg.contains("“"))
    {
        msg = msg.replace("“", "");
        msg = msg.replace("”", "");
        if (msg.length() <= danmuLongest)
            return msg;
    }
    return msg;
}

double MainWindow::getPaletteBgProg() const
{
    return paletteProg;
}

void MainWindow::setPaletteBgProg(double x)
{
    this->paletteProg = x;
}

void MainWindow::startSaveDanmakuToFile()
{
    if (danmuLogFile)
        finishSaveDanmuToFile();

    QDir dir;
    dir.mkdir(rt->dataPath+"danmaku_histories");
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");

    qInfo() << "开启弹幕记录：" << rt->dataPath+"danmaku_histories/" + ac->roomId + "_" + date + ".log";
    danmuLogFile = new QFile(rt->dataPath+"danmaku_histories/" + ac->roomId + "_" + date + ".log");
    danmuLogFile->open(QIODevice::WriteOnly | QIODevice::Append);
    danmuLogStream = new QTextStream(danmuLogFile);
    danmuLogStream->setGenerateByteOrderMark(true);
    if (!externFileCodec.isEmpty())
        danmuLogStream->setCodec(externFileCodec.toUtf8());
}

void MainWindow::finishSaveDanmuToFile()
{
    if (!danmuLogFile)
        return ;

    delete danmuLogStream;
    danmuLogFile->close();
    danmuLogFile->deleteLater();
    danmuLogFile = nullptr;
    danmuLogStream = nullptr;
}

void MainWindow::startCalculateDailyData()
{
    if (dailySettings)
    {
        saveCalculateDailyData();
        dailySettings->deleteLater();
    }

    QDir dir;
    dir.mkdir(rt->dataPath+"live_daily");
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    dailySettings = new QSettings(rt->dataPath+"live_daily/" + ac->roomId + "_" + date + ".ini", QSettings::Format::IniFormat);

    dailyCome = dailySettings->value("come", 0).toInt();
    dailyPeopleNum = dailySettings->value("people_num", 0).toInt();
    dailyDanmaku = dailySettings->value("danmaku", 0).toInt();
    dailyNewbieMsg = dailySettings->value("newbie_msg", 0).toInt();
    dailyNewFans = dailySettings->value("new_fans", 0).toInt();
    dailyTotalFans = dailySettings->value("total_fans", 0).toInt();
    dailyGiftSilver = dailySettings->value("gift_silver", 0).toInt();
    dailyGiftGold = dailySettings->value("gift_gold", 0).toInt();
    dailyGuard = dailySettings->value("guard", 0).toInt();
    dailyMaxPopul = dailySettings->value("max_popularity", 0).toInt();
    dailyAvePopul = 0;
    if (us->currentGuards.size())
        dailySettings->setValue("guard_count", us->currentGuards.size());
    else
        updateExistGuards(0);
}

void MainWindow::saveCalculateDailyData()
{
    if (dailySettings)
    {
        dailySettings->setValue("come", dailyCome);
        dailySettings->setValue("people_num", qMax(dailySettings->value("people_num").toInt(), us->userComeTimes.size()));
        dailySettings->setValue("danmaku", dailyDanmaku);
        dailySettings->setValue("newbie_msg", dailyNewbieMsg);
        dailySettings->setValue("new_fans", dailyNewFans);
        dailySettings->setValue("total_fans", ac->currentFans);
        dailySettings->setValue("gift_silver", dailyGiftSilver);
        dailySettings->setValue("gift_gold", dailyGiftGold);
        dailySettings->setValue("guard", dailyGuard);
        if (us->currentGuards.size())
            dailySettings->setValue("guard_count", us->currentGuards.size());
    }
}

void MainWindow::saveTouta()
{
    us->setValue("pk/toutaCount", toutaCount);
    us->setValue("pk/chiguaCount", chiguaCount);
    us->setValue("pk/toutaGold", toutaGold);
    ui->pkAutoMelonCheck->setToolTip(QString("偷塔次数：%1\n吃瓜数量：%2\n金瓜子数：%3").arg(toutaCount).arg(chiguaCount).arg(toutaGold));
}

void MainWindow::restoreToutaGifts(QString text)
{
    toutaGifts.clear();
    QStringList sl = text.split("\n", QString::SkipEmptyParts);
    foreach (QString s, sl)
    {
        QStringList l = s.split(" ", QString::SkipEmptyParts);
        // 礼物ID 名字 金瓜子
        if (l.size() < 3)
            continue;
        int id = l.at(0).toInt();
        QString name = l.at(1);
        int gold = l.at(2).toInt();
        toutaGifts.append(LiveDanmaku("", id, name, 1, 0, QDateTime(), "gold", gold));
    }
}

void MainWindow::initLiveRecord()
{
    connect(&m3u8Downloader, &M3u8Downloader::signalProgressChanged, this, [=](qint64 size) {
        qint64 MB = size / 1024 / 1024; // 单位：M
        ui->recordCheck->setToolTip("文件大小：" + snum(MB) + " M");
    });
    connect(&m3u8Downloader, &M3u8Downloader::signalTimeChanged, this, [=](qint64 msecond) {
        qint64 second = msecond / 1000;
        qint64 hour = second / 60 / 60;
        qint64 mint = second / 60 % 60;
        qint64 secd = second % 60;
        QString timeStr;
        if (hour > 0)
        timeStr = QString("录制中：%1:%2:%3").arg(hour, 2, 10, QLatin1Char('0'))
                .arg(mint, 2, 10, QLatin1Char('0'))
                .arg(secd, 2, 10, QLatin1Char('0'));
        else
            timeStr = QString("录制中：%2:%3").arg(mint, 2, 10, QLatin1Char('0'))
                                             .arg(secd, 2, 10, QLatin1Char('0'));
        ui->recordCheck->setText(timeStr);
    });
    connect(&m3u8Downloader, &M3u8Downloader::signalSaved, this, [=](QString path) {
        // 发送事件
        LiveDanmaku dmk;
        dmk.setText(path);
        QString fileName = QFileInfo(path).fileName();
        dmk.setNickname(fileName);
        triggerCmdEvent("LIVE_RECORD_SAVED", dmk);

        // 进行格式转换
        if (ui->recordFormatCheck->isChecked())
        {
            QDir dir = QFileInfo(path).dir();
            QString newPath = QFileInfo(dir.absoluteFilePath(fileName + ".mp4")).absoluteFilePath();

            // 调用 FFmpeg 转换格式
            recordConvertProcess = new QProcess(nullptr);
            QProcess* currPoc = recordConvertProcess;
            QString cmd = QString(rt->ffmpegPath + " -i \"%1\" -c copy \"%2\"").arg(path).arg(newPath);
            connect(currPoc, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error){
                qWarning() << "录播转换出错：" << error << currPoc;
                QString err = snum(error);
                if (rt->ffmpegPath.isEmpty() || !isFileExist(rt->ffmpegPath))
                    err = "FFmpeg文件路径出错";
                showError("录播转换出错", err);
                currPoc->close();
                currPoc->deleteLater();
                if (recordConvertProcess == currPoc)
                    recordConvertProcess = nullptr;
            });
            connect(currPoc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [=](int exitCode, QProcess::ExitStatus exitStatus) {
                qInfo() << "录播转换格式保存路径：" << newPath;
                LiveDanmaku dmk;
                dmk.setText(newPath);
                dmk.setNickname(fileName);
                triggerCmdEvent("LIVE_RECORD_FORMATTED", dmk);
                currPoc->close();
                currPoc->deleteLater();
                if (recordConvertProcess == currPoc)
                    recordConvertProcess = nullptr;
                QTimer::singleShot(1000, [=]{
                    deleteFile(path); // 延迟删除旧文件，怕被占用
                });
            });
            currPoc->start(cmd);
        }

        ui->recordCheck->setText("原画录播");
    });
}

void MainWindow::startLiveRecord()
{
    if (m3u8Downloader.isDownloading())
        finishLiveRecord();
    if (ac->roomId.isEmpty())
        return ;

    getRoomLiveVideoUrl([=](QString url){
        // 检查能否下载
        /* recordUrl = "";
        qInfo() << "准备录播：" << url;
        QNetworkAccessManager manager;
        QNetworkRequest* request = new QNetworkRequest(QUrl(url));
        QEventLoop* loop = new QEventLoop();
        QNetworkReply *reply = manager.get(*request);

        connect(reply, SIGNAL(finished()), loop, SLOT(quit())); //请求结束并下载完成后，退出子事件循环
        connect(reply, &QNetworkReply::downloadProgress, loop, [=](qint64 bytesReceived, qint64 bytesTotal){
            if (bytesReceived > 0)
            {
                // qInfo() << "原始地址可直接下载";
                recordUrl = url;
                loop->quit();
            }
        });
        loop->exec(); //开启子事件循环
        loop->deleteLater();

        // B站下载会有302重定向的，需要获取headers里面的Location值
        QString forward = reply->rawHeader("location").trimmed();
        if (recordUrl.isEmpty() || !forward.isEmpty())
        {
            recordUrl = forward;
            qInfo() << "录播实际地址：" << recordUrl;
        }
        delete request;
        reply->deleteLater();
        if (recordUrl.isEmpty())
        {
            qWarning() << "无法获取下载地址" << forward;
            return ;
        }

        // 开始下载文件
        startRecordUrl(recordUrl); */

        startRecordUrl(url);
    });
}

void MainWindow::startRecordUrl(QString url)
{
    qInfo() << "开始录播:" << url << QDateTime::currentDateTime();
    qint64 startSecond = QDateTime::currentSecsSinceEpoch();
    QDir dir(rt->dataPath);
    dir.mkpath("record");
    dir.cd("record");
    QString fileName = ac->roomId + "_" + QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss");
    QString suffix = ".flv";
    if (url.contains(".m3u8"))
        suffix = ".ts";
    QString path = QFileInfo(dir.absoluteFilePath(fileName + suffix)).absoluteFilePath();
    recordLastPath = path;

    ui->recordCheck->setText("录制中...");
    recordTimer->start();
    startRecordTime = QDateTime::currentMSecsSinceEpoch();

    // 开始下载
    m3u8Downloader.start(url, path);

    return ;
    /* recordLoop = new QEventLoop;
    QNetworkAccessManager manager;
    QNetworkRequest* request = new QNetworkRequest(QUrl(url));
    request->setHeader(QNetworkRequest::CookieHeader, ac->userCookies);
    QNetworkReply *reply = manager.get(*request);
    // 写入文件
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Append))
    {
        qCritical() << "写入文件失败" << path;
        reply->deleteLater();
        return ;
    }
    QObject::connect(reply, SIGNAL(finished()), recordLoop, SLOT(quit())); //请求结束并下载完成后，退出子事件循环

    // 下载进度：progress=下载总大小（单位Byte）, total=-1
    connect(reply, &QNetworkReply::downloadProgress, this, [=](qint64 progress, qint64 total){
        qint64 size = progress / 1024 / 1024; // 单位：M
        ui->recordCheck->setToolTip("文件大小：" + snum(size) + " M");
    });
    // 实时保存到缓冲区
    connect(reply, &QNetworkReply::readyRead, this, [&]{
        QByteArray data = reply->read(1024*1024*1024);
        qDebug() << "保存大小：" << data.size();
        file.write(data);
        qint64 second = QDateTime::currentSecsSinceEpoch() - startSecond;
        qint64 hour = second / 60 / 60;
        qint64 mint = second / 60 % 60;
        qint64 secd = second % 60;
        QString timeStr;
        if (hour > 0)
        timeStr = QString("录制中：%1:%2:%3").arg(hour, 2, 10, QLatin1Char('0'))
                .arg(mint, 2, 10, QLatin1Char('0'))
                .arg(secd, 2, 10, QLatin1Char('0'));
        else
            timeStr = QString("录制中：%2:%3").arg(mint, 2, 10, QLatin1Char('0'))
                                             .arg(secd, 2, 10, QLatin1Char('0'));
        ui->recordCheck->setText(timeStr);
    });

    // 开启子事件循环
    recordLoop->exec();
    disconnect(reply, nullptr, recordLoop, nullptr);
    recordLoop->deleteLater();
    recordLoop = nullptr;

    // 刷新缓冲区，永久保存到磁盘
    file.flush();
    qInfo() << "录播完成，保存至：" << path;

    // 发送事件
    LiveDanmaku dmk;
    dmk.setText(path);
    dmk.setNickname(fileName);
    triggerCmdEvent("LIVE_RECORD_SAVED", dmk);

    // 进行格式转换
    if (ui->recordFormatCheck->isChecked())
    {
        QString newPath = QFileInfo(dir.absoluteFilePath(fileName + ".mp4")).absoluteFilePath();

        // 调用 FFmpeg 转换格式
        recordConvertProcess = new QProcess(nullptr);
        QProcess* currPoc = recordConvertProcess;
        QString cmd = QString(rt->ffmpegPath + " -i \"%1\" -c copy \"%2\"").arg(path).arg(newPath);
        connect(currPoc, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error){
            qWarning() << "录播转换出错：" << error << currPoc;
            QString err = snum(error);
            if (rt->ffmpegPath.isEmpty() || !isFileExist(rt->ffmpegPath))
                err = "FFmpeg文件路径出错";
            showError("录播转换出错", err);
            currPoc->close();
            currPoc->deleteLater();
            if (recordConvertProcess == currPoc)
                recordConvertProcess = nullptr;
        });
        connect(currPoc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [=](int exitCode, QProcess::ExitStatus exitStatus) {
            qInfo() << "录播转换格式保存路径：" << newPath;
            LiveDanmaku dmk;
            dmk.setText(newPath);
            dmk.setNickname(fileName);
            triggerCmdEvent("LIVE_RECORD_FORMATTED", dmk);
            currPoc->close();
            currPoc->deleteLater();
            if (recordConvertProcess == currPoc)
                recordConvertProcess = nullptr;
            QTimer::singleShot(1000, [=]{
                deleteFile(path); // 延迟删除旧文件，怕被占用
            });
        });
        currPoc->start(cmd);
    }

    // 删除变量
    reply->deleteLater();
    delete request;
    startRecordTime = 0;
    ui->recordCheck->setText("原画录播");
    recordTimer->stop();

    // 可能是时间结束了，重新下载
    if (ui->recordCheck->isChecked() && isLiving())
    {
        startLiveRecord();
    } */
}

/**
 * 结束录制
 * 可能是点击按钮取消录制
 * 也可能是时间到了，需要重新录制
 */
void MainWindow::finishLiveRecord()
{
    if (m3u8Downloader.isDownloading())
    {
        m3u8Downloader.stop();

        // 可能是时间结束了，重新下载
        if (ui->recordCheck->isChecked() && isLiving())
        {
            startLiveRecord();
        }
    }

    /* if (!recordLoop)
        return ;
    qInfo() << "结束录播";
    recordLoop->quit();
    recordLoop = nullptr; */
}

void MainWindow::processRemoteCmd(QString msg, bool response)
{
    if (!remoteControl)
        return ;

    if (msg == "关闭功能")
    {
        processRemoteCmd("关闭欢迎", false);
        processRemoteCmd("关闭关注答谢", false);
        processRemoteCmd("关闭送礼答谢", false);
        processRemoteCmd("关闭禁言", false);
        if (response)
            sendNotifyMsg(">已暂停自动弹幕");
    }
    else if (msg == "开启功能")
    {
        processRemoteCmd("开启欢迎", false);
        processRemoteCmd("开启关注答谢", false);
        processRemoteCmd("开启送礼答谢", false);
        processRemoteCmd("开启禁言", false);
        if (response)
            sendNotifyMsg(">已开启自动弹幕");
    }
    else if (msg == "关闭机器人")
    {
        socket->abort();
        connectServerTimer->stop();
    }
    else if (msg == "关闭欢迎")
    {
        ui->autoSendWelcomeCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动欢迎");
    }
    else if (msg == "开启欢迎")
    {
        ui->autoSendWelcomeCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动欢迎");
    }
    else if (msg == "关闭关注答谢")
    {
        ui->autoSendAttentionCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动答谢关注");
    }
    else if (msg == "开启关注答谢")
    {
        ui->autoSendAttentionCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动答谢关注");
    }
    else if (msg == "关闭送礼答谢")
    {
        ui->autoSendGiftCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动答谢送礼");
    }
    else if (msg == "开启送礼答谢")
    {
        ui->autoSendGiftCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动答谢送礼");
    }
    else if (msg == "关闭禁言")
    {
        ui->autoBlockNewbieCheck->setChecked(false);
        on_autoBlockNewbieCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停新人关键词自动禁言");
    }
    else if (msg == "开启禁言")
    {
        ui->autoBlockNewbieCheck->setChecked(true);
        on_autoBlockNewbieCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启新人关键词自动禁言");
    }
    else if (msg == "关闭偷塔")
    {
        ui->pkAutoMelonCheck->setChecked(false);
        on_pkAutoMelonCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停自动偷塔");
    }
    else if (msg == "开启偷塔")
    {
        ui->pkAutoMelonCheck->setChecked(true);
        on_pkAutoMelonCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启自动偷塔");
    }
    else if (msg == "关闭点歌")
    {
        ui->DiangeAutoCopyCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动点歌");
    }
    else if (msg == "开启点歌")
    {
        ui->DiangeAutoCopyCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动点歌");
    }
    else if (msg == "关闭点歌回复")
    {
        ui->diangeReplyCheck->setChecked(false);
        on_diangeReplyCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停自动点歌回复");
    }
    else if (msg == "开启点歌回复")
    {
        ui->diangeReplyCheck->setChecked(true);
        on_diangeReplyCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启自动点歌回复");
    }
    else if (msg == "关闭自动连接")
    {
        ui->timerConnectServerCheck->setChecked(false);
        on_timerConnectServerCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停自动连接");
    }
    else if (msg == "开启自动连接")
    {
        ui->timerConnectServerCheck->setChecked(true);
        on_timerConnectServerCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启自动连接");
    }
    else if (msg == "关闭定时任务")
    {
        for (int i = 0; i < ui->taskListWidget->count(); i++)
        {
            QListWidgetItem* item = ui->taskListWidget->item(i);
            auto widget = ui->taskListWidget->itemWidget(item);
            auto tw = static_cast<TaskWidget*>(widget);
            tw->check->setChecked(false);
        }
        saveTaskList();
        if (response)
            sendNotifyMsg(">已关闭定时任务");
    }
    else if (msg == "开启定时任务")
    {
        for (int i = 0; i < ui->taskListWidget->count(); i++)
        {
            QListWidgetItem* item = ui->taskListWidget->item(i);
            auto widget = ui->taskListWidget->itemWidget(item);
            auto tw = static_cast<TaskWidget*>(widget);
            tw->check->setChecked(true);
        }
        saveTaskList();
        if (response)
            sendNotifyMsg(">已开启定时任务");
    }
    else if (msg == "开启录播")
    {
        startLiveRecord();
        if (response)
            sendNotifyMsg(">已开启录播");
    }
    else if (msg == "关闭录播")
    {
        finishLiveRecord();
        if (response)
            sendNotifyMsg(">已关闭录播");
    }
    else if (msg == "撤销禁言")
    {
        if (!blockedQueue.size())
        {
            sendNotifyMsg(">没有可撤销的禁言用户");
            return ;
        }

        LiveDanmaku danmaku = blockedQueue.takeLast();
        delBlockUser(danmaku.getUid());
        if (us->eternalBlockUsers.contains(EternalBlockUser(danmaku.getUid(), ac->roomId.toLongLong())))
        {
            us->eternalBlockUsers.removeOne(EternalBlockUser(danmaku.getUid(), ac->roomId.toLongLong()));
            saveEternalBlockUsers();
        }
        if (response)
            sendNotifyMsg(">已解除禁言：" + danmaku.getNickname());
    }
    else if (msg.startsWith("禁言 "))
    {
        QRegularExpression re("^禁言\\s*(\\S+)\\s*(\\d+)?$");
        QRegularExpressionMatch match;
        if (msg.indexOf(re, 0, &match) == -1)
            return ;
        QString nickname = match.captured(1);
        QString hours = match.captured(2);
        int hour = ui->autoBlockTimeSpin->value();
        if (!hours.isEmpty())
            hour = hours.toInt();

    }
    else if (msg.startsWith("解禁 ") || msg.startsWith("解除禁言 ") || msg.startsWith("取消禁言 "))
    {
        QRegularExpression re("^(?:解禁|解除禁言|取消禁言)\\s*(.+)\\s*$");
        QRegularExpressionMatch match;
        if (msg.indexOf(re, 0, &match) == -1)
            return ;
        QString nickname = match.captured(1);

        // 优先遍历禁言的
        for (int i = blockedQueue.size()-1; i >= 0; i--)
        {
            const LiveDanmaku danmaku = blockedQueue.at(i);
            if (!danmaku.is(MSG_DANMAKU))
                continue;

            QString nick = danmaku.getNickname();
            if (nick.contains(nickname))
            {
                if (us->eternalBlockUsers.contains(EternalBlockUser(danmaku.getUid(), ac->roomId.toLongLong())))
                {
                    us->eternalBlockUsers.removeOne(EternalBlockUser(danmaku.getUid(), ac->roomId.toLongLong()));
                    saveEternalBlockUsers();
                }

                delBlockUser(danmaku.getUid());
                sendNotifyMsg(">已解禁：" + nick, true);
                blockedQueue.removeAt(i);
                return ;
            }
        }

        // 其次遍历弹幕的
        for (int i = roomDanmakus.size()-1; i >= 0; i--)
        {
            const LiveDanmaku danmaku = roomDanmakus.at(i);
            if (danmaku.getUid() == 0)
                continue;

            QString nick = danmaku.getNickname();
            if (nick.contains(nickname))
            {
                if (us->eternalBlockUsers.contains(EternalBlockUser(danmaku.getUid(), ac->roomId.toLongLong())))
                {
                    us->eternalBlockUsers.removeOne(EternalBlockUser(danmaku.getUid(), ac->roomId.toLongLong()));
                    saveEternalBlockUsers();
                }

                delBlockUser(danmaku.getUid());
                sendNotifyMsg(">已解禁：" + nick, true);
                blockedQueue.removeAt(i);
                return ;
            }
        }
    }
    else if (msg.startsWith("永久禁言 "))
    {
        QRegularExpression re("^永久禁言\\s*(\\S+)\\s*$");
        QRegularExpressionMatch match;
        if (msg.indexOf(re, 0, &match) == -1)
            return ;
        QString nickname = match.captured(1);
        for (int i = roomDanmakus.size()-1; i >= 0; i--)
        {
            const LiveDanmaku danmaku = roomDanmakus.at(i);
            if (!danmaku.is(MSG_DANMAKU))
                continue;

            QString nick = danmaku.getNickname();
            if (nick.contains(nickname))
            {
                eternalBlockUser(danmaku.getUid(), danmaku.getNickname());
                sendNotifyMsg(">已永久禁言：" + nick, true);
                return ;
            }
        }
    }
    else if (msg == "开启弹幕回复")
    {
        ui->AIReplyMsgCheck->setCheckState(Qt::CheckState::Checked);
        ui->AIReplyCheck->setChecked(true);
        on_AIReplyMsgCheck_clicked();
        if (!danmakuWindow)
            on_actionShow_Live_Danmaku_triggered();
        if (response)
            sendNotifyMsg(">已开启弹幕回复");
    }
    else if (msg == "关闭弹幕回复")
    {
        ui->AIReplyMsgCheck->setChecked(Qt::CheckState::Unchecked);
        on_AIReplyMsgCheck_clicked();
        if (us->value("danmaku/aiReply", false).toBool())
            ui->AIReplyCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已关闭弹幕回复");
    }
    else
        return ;
    qInfo() << "执行远程命令：" << msg;
}

bool MainWindow::execFunc(QString msg, LiveDanmaku& danmaku, CmdResponse &res, int &resVal)
{
    // 语法糖
    QRegularExpressionMatch match;
    if (ui->syntacticSugarCheck->isChecked())
    {
        // {key} = val
        // {key} += val
        QRegularExpression re("^\\s*\\{(.+?)\\}\\s*(.?)=\\s*(.*)\\s*$");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QString key = match.captured(1);
            QString ope = match.captured(2); // 操作符
            QString val = match.captured(3);
            if (!key.contains("/"))
                key = "heaps/" + key;
            if (ope.isEmpty())
            {
                heaps->setValue(key, val);
                qInfo() << "set value" << key << "=" << val;
            }
            else // 数值运算
            {
                qint64 v = heaps->value(key).toLongLong();
                qint64 x = val.toLongLong();
                if (ope == "+")
                    v += x;
                else if (ope == "-")
                    v -= x;
                else if (ope == "*")
                    v *= x;
                else if (ope == "/")
                {
                    if (x == 0)
                    {
                        showError("错误的/运算", msg);
                        x = 1;
                    }
                    v /= x;
                }
                else if (ope == "%")
                {
                    if (x == 0)
                    {
                        showError("错误的%运算", msg);
                        x = 1;
                    }
                    v /= x;
                }
                heaps->setValue(key, v);
                qInfo() << "set value" << key << "=" << v;
            }
            return true;
        }

        // {key}++  {key}--
        re = QRegularExpression("^\\s*\\{(.+)\\}\\s*(\\+\\+|\\-\\-)\\s*$");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QString key = match.captured(1);
            QString ope = match.captured(2);
            if (!key.contains("/"))
                key = "heaps/" + key;
            qint64 v = heaps->value(key).toLongLong();
            if (ope == "++")
                v++;
            else if (ope == "--")
                v--;
            heaps->setValue(key, v);
            qInfo() << "set value" << key << "=" << v;
            return true;
        }
    }

    // 总体判断判断是不是 >func() 格式的命令
    QRegularExpression re("^\\s*>");
    if (msg.indexOf(re) == -1)
        return false;

    // qDebug() << "尝试执行命令：" << msg;
    auto RE = [=](QString exp) -> QRegularExpression {
        return QRegularExpression("^\\s*>\\s*" + exp + "\\s*$");
    };

    // 过滤器
    if (msg.contains("reject"))
    {
        re = RE("reject\\s*\\(\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            return true;
        }
    }

    // 禁言
    if (msg.contains("block"))
    {
        re = RE("block\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            int hour = caps.at(2).toInt();
            addBlockUser(uid, hour);
            return true;
        }
        re = RE("block\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            int hour = ui->autoBlockTimeSpin->value();
            addBlockUser(uid, hour);
            return true;
        }
    }

    // 解禁言
    if (msg.contains("unblock"))
    {
        re = RE("unblock\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            delBlockUser(uid);
            return true;
        }
    }

    // 永久禁言
    if (msg.contains("eternalBlock"))
    {
        re = RE("eternalBlock\\s*\\(\\s*(\\d+)\\s*,\\s*(\\S+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            QString uname = caps.at(2);
            eternalBlockUser(uid, uname);
            return true;
        }
    }

    // 任命房管
    if (msg.contains("appointAdmin"))
    {
        re = RE("appointAdmin\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            appointAdmin(uid);
            return true;
        }
    }

    // 取消房管
    if (msg.contains("dismissAdmin"))
    {
        re = RE("dismissAdmin\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            dismissAdmin(uid);
            return true;
        }
    }

    // 赠送礼物
    if (msg.contains("sendGift"))
    {
        re = RE("sendGift\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            int giftId = caps.at(1).toInt();
            int num = caps.at(2).toInt();
            sendGift(giftId, num);
            return true;
        }
    }

    // 终止
    if (msg.contains("abort"))
    {
        re = RE("abort\\s*(\\(\\s*\\))?");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            res = AbortRes;
            return true;
        }
    }

    // 延迟
    if (msg.contains("delay"))
    {
        re = RE("delay\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            int delay = caps.at(1).toInt(); // 单位：毫秒
            res = DelayRes;
            resVal = delay;
            return true;
        }
    }

    // 添加到游戏用户
    if (msg.contains("addGameUser"))
    {
        re = RE("addGameUser\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameUsers[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameUsers[chan].append(uid);
            return true;
        }

        re = RE("addGameUser\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameUsers[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameUsers[0].append(uid);
            return true;
        }
    }

    // 从游戏用户中移除
    if (msg.contains("removeGameUser"))
    {
        re = RE("removeGameUser\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameUsers[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameUsers[chan].removeOne(uid);
            return true;
        }

        re = RE("removeGameUser\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameUsers[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameUsers[0].removeOne(uid);
            return true;
        }
    }

    // 添加到游戏数值
    if (msg.contains("addGameNumber"))
    {
        re = RE("addGameNumber\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameNumberLists[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameNumberLists[chan].append(uid);
            saveGameNumbers(chan);
            return true;
        }

        re = RE("addGameNumber\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameNumberLists[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameNumberLists[0].append(uid);
            saveGameNumbers(0);
            return true;
        }
    }

    // 从游戏数值中移除
    if (msg.contains("removeGameNumber"))
    {
        re = RE("removeGameNumber\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameNumberLists[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameNumberLists[chan].removeOne(uid);
            saveGameNumbers(chan);
            return true;
        }

        re = RE("removeGameNumber\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameNumberLists[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameNumberLists[0].removeOne(uid);
            saveGameNumbers(0);
            return true;
        }
    }

    // 添加到文本数值
    if (msg.contains("addGameText"))
    {
        re = RE("addGameText\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameTextLists[0].size();
            int chan = caps.at(1).toInt();
            QString text = caps.at(2);
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameTextLists[chan].append(text);
            saveGameTexts(chan);
            return true;
        }

        re = RE("addGameText\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameTextLists[0].size();
            QString text = caps.at(1);
            gameTextLists[0].append(text);
            saveGameTexts(0);
            return true;
        }
    }

    // 从游戏文本中移除
    if (msg.contains("removeGameText"))
    {
        re = RE("removeGameText\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameTextLists[0].size();
            int chan = caps.at(1).toInt();
            QString text = caps.at(2);
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameTextLists[chan].removeOne(text);
            saveGameTexts(chan);
            return true;
        }

        re = RE("removeGameText\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps << gameTextLists[0].size();
            QString text = caps.at(1);
            gameTextLists[0].removeOne(text);
            saveGameTexts(0);
            return true;
        }
    }

    // 执行远程命令
    if (msg.contains("execRemoteCommand"))
    {
        re = RE("execRemoteCommand\\s*\\(\\s*(.+?)\\s*,\\s*(\\d)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            int response = caps.at(2).toInt();
            qInfo() << "执行命令：" << caps;
            processRemoteCmd(cmd, response);
            return true;
        }

        re = RE("execRemoteCommand\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            qInfo() << "执行命令：" << caps;
            processRemoteCmd(cmd);
            return true;
        }
    }

    // 发送私信
    if (msg.contains("sendPrivateMsg"))
    {
        re = RE("sendPrivateMsg\\s*\\(\\s*(\\d+)\\s*,\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            QString msg = caps.at(2);
            qInfo() << "执行命令：" << caps;
            msg = toMultiLine(msg);
            sendPrivateMsg(uid, msg);
            return true;
        }
    }

    // 发送指定直播间弹幕
    if (msg.contains("sendRoomMsg"))
    {
        re = RE("sendRoomMsg\\s*\\(\\s*(\\d+)\\s*,\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString roomId = caps.at(1);
            QString msg = caps.at(2);
            qInfo() << "执行命令：" << caps;
            sendRoomMsg(roomId, msg);
            return true;
        }
    }

    // 定时操作
    if (msg.contains("timerShot"))
    {
        re = RE("timerShot\\s*\\(\\s*(\\d+)\\s*,\\s*?(.*)\\s*?\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            int time = caps.at(1).toInt();
            QString msg = caps.at(2);
            msg = toRunableCode(toMultiLine(msg));
            QTimer::singleShot(time, this, [=]{
                LiveDanmaku ld = danmaku;
                sendAutoMsg(msg, danmaku);
            });
            return true;
        }
    }

    // 发送本地通知
    if (msg.contains("localNotify"))
    {
        re = RE("localNotify\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString msg = caps.at(1);
            msg = toMultiLine(msg);
            qInfo() << "执行命令：" << caps;
            localNotify(msg);
            return true;
        }

        re = RE("localNotify\\s*\\((\\d+)\\s*,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString uid = caps.at(1);
            QString msg = caps.at(2);
            msg = toMultiLine(msg);
            qInfo() << "执行命令：" << caps;
            localNotify(msg, uid.toLongLong());
            return true;
        }
    }

    // 朗读文本
    if (msg.contains("speakText"))
    {
        re = RE("speakText\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = toMultiLine(caps.at(1));
            qInfo() << "执行命令：" << caps;
            speakText(text);
            return true;
        }

        /* re = RE("speakTextXfy\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            if (!xfyTTS)
            {
                VoicePlatform temp = voicePlatform;
                voicePlatform = VoiceXfy;
                initTTS();
                voicePlatform = temp;
            }
            xfyTTS->speakText(text);
            return true;
        } */

        re = RE("speakTextSSML\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = toMultiLine(caps.at(1));
            qInfo() << "执行命令：" << caps;
            if (!msTTS)
            {
                VoicePlatform temp = voicePlatform;
                voicePlatform = VoiceMS;
                initTTS();
                voicePlatform = temp;
            }
            msTTS->speakSSML(text);
            return true;
        }

        re = RE("speakTextUrl\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString link = caps.at(1).trimmed();
            qInfo() << "执行命令：" << caps;
            playNetAudio(link);
            return true;
        }
    }

    if (msg.contains("setVoice"))
    {
        // 发音人
        re = RE("setVoiceSpeaker\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            ui->voiceNameEdit->setText(text);
            on_voiceNameEdit_editingFinished();
            return true;
        }

        // 音速
        re = RE("setVoiceSpeed\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            bool ok;
            int val = caps.at(1).toInt(&ok);
            if (!ok)
            {
                showError("setVoiceSpeed", "无法识别的数字：" + caps.at(1));
                return true;
            }
            qInfo() << "执行命令：" << caps;
            ui->voiceSpeedSlider->setValue(val);
            on_voiceSpeedSlider_valueChanged(val);
            return true;
        }

        // 音调
        re = RE("setVoicePitch\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            bool ok;
            int val = caps.at(1).toInt(&ok);
            if (!ok)
            {
                showError("setVoicePitch", "无法识别的数字：" + caps.at(1));
                return true;
            }
            qInfo() << "执行命令：" << caps;
            ui->voicePitchSlider->setValue(val);
            on_voicePitchSlider_valueChanged(val);
            return true;
        }

        // 音量
        re = RE("setVoiceVolume\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            bool ok;
            int val = caps.at(1).toInt(&ok);
            if (!ok)
            {
                showError("setVoiceSpeed", "无法识别的数字：" + caps.at(1));
                return true;
            }
            qInfo() << "执行命令：" << caps;
            ui->voiceVolumeSlider->setValue(val);
            on_voiceVolumeSlider_valueChanged(val);
            return true;
        }
    }

    // 连接直播间
    if (msg.contains("connectRoom"))
    {
        re = RE("connectRoom\\s*\\(\\s*(\\w+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString rm = caps.at(1);
            qInfo() << "执行命令：" << caps;
            ui->roomIdEdit->setText(rm);
            on_roomIdEdit_editingFinished();
            return true;
        }
    }

    // 网络操作
    if (msg.contains("openUrl"))
    {
        re = RE("openUrl\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString url = caps.at(1);
            qInfo() << "执行命令：" << caps;
            openLink(url);
            return true;
        }
    }

    // 后台网络操作
    if (msg.contains("connectNet"))
    {
        re = RE("connectNet\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString url = caps.at(1);
            get(url, [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qInfo() << QString(ba);
            });
            return true;
        }
    }
    if (msg.contains("getData"))
    {
        re = RE("getData\\s*\\(\\s*(.+)\\s*,\\s*(\\S*?)\\s*\\)"); // 带参数二
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("getData\\s*\\(\\s*(.+)\\s*\\)"); // 不带参数二
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString callback = caps.size() > 2 ? caps.at(2) : "";
            get(url, [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qInfo() << QString(ba);
                if (!callback.isEmpty())
                {
                    LiveDanmaku dmk;
                    dmk.with(MyJson(ba));
                    dmk.setText(ba);
                    triggerCmdEvent(callback, dmk);
                }
            });
            return true;
        }
    }
    if (msg.contains("postData"))
    {
        re = RE("postData\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*,\\s*(\\S+?)\\s*\\)"); // 带参数三
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("postData\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*\\)"); // 不带参数三
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString data = caps.at(2);
            QString callback = caps.size() > 3 ? caps.at(3) : "";
            data = toMultiLine(data);
            post(url, data.toStdString().data(), [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qInfo() << QString(ba);
                if (!callback.isEmpty())
                {
                    LiveDanmaku dmk;
                    dmk.with(MyJson(ba));
                    dmk.setText(ba);
                    triggerCmdEvent(callback, dmk);
                }
            });
            return true;
        }
    }
    if (msg.contains("postHeaderData"))
    {
        re = RE("postHeaderData\\s*\\(\\s*(.+?)\\s*,\\s*(.*?)\\s*,\\s*(.*)\\s*,\\s*(\\S+?)\\s*\\)"); // 带参数三
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("postHeaderData\\s*\\(\\s*(.+?)\\s*,\\s*(.*?)\\s*,\\s*(.*)\\s*\\)"); // 不带参数三
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString headerS = caps.at(2);
            QString data = caps.at(3);
            QString callback = caps.size() > 4 ? caps.at(4) : "";
            data = toMultiLine(data);
            QStringList headers = headerS.split("&", QString::SkipEmptyParts);

            // 开始联网
            QNetworkAccessManager* manager = new QNetworkAccessManager;
            QNetworkRequest* request = new QNetworkRequest(url);
            setUrlCookie(url, request);
            request->setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
            request->setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.111 Safari/537.36");
            for (auto header: headers)
            {
                int find = header.indexOf("=");
                if (find == -1)
                {
                    qWarning() << "错误的 Post Header：" << header;
                    continue;
                }
                QString key = header.left(find);
                QString val = header.right(header.length() - find - 1);
                request->setRawHeader(key.toStdString().data(), val.toStdString().data());
            }
            // 连接槽
            QObject::connect(manager, &QNetworkAccessManager::finished, me, [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qInfo() << QString(ba);
                if (!callback.isEmpty())
                {
                    LiveDanmaku dmk;
                    dmk.with(MyJson(ba));
                    dmk.setText(ba);
                    triggerCmdEvent(callback, dmk);
                }

                manager->deleteLater();
                delete request;
                reply->deleteLater();
            });

            manager->post(*request, data.toStdString().data());
            return true;
        }
    }
    if (msg.contains("postJson"))
    {
        re = RE("postJson\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*,\\s*(\\S+?)\\s*\\)"); // 带参数三
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("postJson\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*\\)"); // 不带参数三
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString data = caps.at(2);
            data = toMultiLine(data);
            QString callback = caps.size() > 3 ? caps.at(3) : "";
            postJson(url, data.toStdString().data(), [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qInfo() << QString(ba);
                if (!callback.isEmpty())
                {
                    LiveDanmaku dmk;
                    dmk.with(MyJson(ba));
                    dmk.setText(ba);
                    triggerCmdEvent(callback, dmk);
                }
            });
            return true;
        }
    }
    if (msg.contains("downloadFile"))
    {
        re = RE("downloadFile\\s*\\(\\s*(.+?)\\s*,\\s*(.+)\\s*,\\s*(\\S+?)\\s*\\)"); // 带参数三
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("downloadFile\\s*\\(\\s*(.+?)\\s*,\\s*(.+)\\s*\\)"); // 不带参数三
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString path = caps.at(2);
            QString callback = caps.size() > 3 ? caps.at(3) : "";
            get(url, [=](QNetworkReply* reply) {
                QByteArray ba = reply->readAll();
                if (!ba.size())
                {
                    showError("下载文件", "空文件：" + url);
                    return ;
                }

                QFile file(path);
                if (!file.open(QIODevice::WriteOnly))
                {
                    showError("下载文件", "保存失败：" + path);
                    return ;
                }
                file.write(ba);
                file.close();
                if (!callback.isEmpty())
                {
                    LiveDanmaku ld(danmaku);
                    ld.setText(path);
                    triggerCmdEvent(callback, ld);
                }
            });
            return true;
        }
    }

    // 发送socket
    if (msg.contains("sendToSockets"))
    {
        re = RE("sendToSockets\\s*\\(\\s*(\\S+),\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            QString data = caps.at(2);
            data = toMultiLine(data);

            qInfo() << "执行命令：" << caps;
            sendTextToSockets(cmd, data.toUtf8());
            return true;
        }
    }
    if (msg.contains("sendToLastSocket"))
    {
        re = RE("sendToLastSocket\\s*\\(\\s*(\\S+),\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            QString data = caps.at(2);
            data = toMultiLine(data);

            qInfo() << "执行命令：" << caps;
            if (danmakuSockets.size())
                sendTextToSockets(cmd, data.toUtf8(), danmakuSockets.last());
            return true;
        }
    }

    // 命令行
    if (msg.contains("runCommandLine"))
    {
        re = RE("runCommandLine\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            cmd = toMultiLine(cmd);
            qInfo() << "执行命令：" << caps;
            QProcess p(nullptr);
            p.start(cmd);
            p.waitForStarted();
            p.waitForFinished();
            qInfo() << QString::fromLocal8Bit(p.readAllStandardError());
            return true;
        }
    }

    // 命令行
    if (msg.contains("startProgram"))
    {
        re = RE("startProgram\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            cmd = toMultiLine(cmd);
            qInfo() << "执行命令：" << caps;
            QProcess p(nullptr);
            p.startDetached(cmd);
            return true;
        }
    }

    // 打开文件
    if (msg.contains("openFile"))
    {
        re = RE("openFile\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString path = caps.at(1);
            qInfo() << "执行命令：" << caps;
#ifdef Q_OS_WIN32
            path = QString("file:///") + path;
            bool is_open = QDesktopServices::openUrl(QUrl(path, QUrl::TolerantMode));
            if(!is_open)
                qWarning() << "打开文件失败";
#else
            QString  cmd = QString("xdg-open ")+ path; //在linux下，可以通过system来xdg-open命令调用默认程序打开文件；
            system(cmd.toStdString().c_str());
#endif
            return true;
        }
    }

    // 写入文件
    if (msg.contains("writeTextFile"))
    {
        re = RE("writeTextFile\\s*\\(\\s*(.+?)\\s*\\,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            QString text = caps.at(2);
            text = toMultiLine(text);
            QFileInfo info(fileName);
            QDir dir = info.absoluteDir();
            dir.mkpath(dir.absolutePath());
            writeTextFile(info.absoluteFilePath(), text);
            qInfo() << "写入文件：" << fileName;
            return true;
        }
    }

    // 写入文件行
    if (msg.contains("appendFileLine"))
    {
        re = RE("appendFileLine\\s*\\(\\s*(.+?)\\s*\\,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            QString format = caps.at(2);
            format = toMultiLine(format);
            QFileInfo info(fileName);
            QDir dir = info.absoluteDir();
            dir.mkpath(dir.absolutePath());
            appendFileLine(fileName, format, lastDanmaku);
            qInfo() << "修改文件：" << fileName;
            return true;
        }
    }

    // 插入文件锚点
    if (msg.contains("insertFileAnchor"))
    {
        re = RE("insertFileAnchor\\s*\\(\\s*(.+?)\\s*,\\s*(.+?)\\s*\\,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            QString anchor = caps.at(2);
            QString content = caps.at(3);
            QString text = readTextFile(fileName);
            text.replace(anchor, content + anchor);
            if (!codeFileCodec.isEmpty())
                writeTextFile(fileName, text, codeFileCodec);
            else
                writeTextFile(fileName, text);
            qInfo() << "修改文件：" << fileName;
            return true;
        }
    }

    // 移除文件某一行，行数从1开始
    if (msg.contains("removeTextFileLine"))
    {
        re = RE("removeTextFileLine\\s*\\(\\s*(.+?)\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            int line = caps.at(2).toInt();

            QString text = readTextFileAutoCodec(fileName);
            QStringList sl = text.split("\n");
            line--;
            if (line < 0 || line >= sl.size())
            {
                showError("removeTextFileLine", "错误的行数：" + snum(line+1) + "不在1~" + snum(sl.size()) + "之间");
                return true;
            }

            sl.removeAt(line);
            writeTextFile(fileName, sl.join("\n"));
            qInfo() << "修改文件：" << fileName;

            return true;
        }
    }

    // 修改文件行
    if (msg.contains("modifyTextFileLine"))
    {
        re = RE("modifyTextFileLine\\s*\\(\\s*(.+?)\\s*,\\s*(\\d+)\\s*,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            int line = caps.at(2).toInt();
            QString newText = caps.at(3);
            if (newText.length() >= 2 && newText.startsWith("\"") && newText.startsWith("\""))
                newText = newText.mid(1, newText.length() - 2);

            QString text = readTextFileAutoCodec(fileName);
            QStringList sl = text.split("\n");
            line--;
            if (line < 0 || line >= sl.size())
            {
                showError("modifyTextFileLine", "错误的行数：" + snum(line+1) + "不在1~" + snum(sl.size()) + "之间");
                return true;
            }

            sl[line] = newText;
            writeTextFile(fileName, sl.join("\n"));
            qInfo() << "修改文件：" << fileName;

            return true;
        }
    }

    // 删除文件
    if (msg.contains("removeFile"))
    {
        re = RE("removeFile\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            QFile file(fileName);
            file.remove();
            qInfo() << "删除文件：" << fileName;
            return true;
        }
    }

    // 文件每一行
    if (msg.contains("fileEachLine"))
    {
        re = RE("fileEachLine\\s*\\(\\s*(.+?)\\s*,(?:\\s*(\\d+),)?\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            QString startLineS = caps.at(2);
            int startLine = 0;
            if (!startLineS.isEmpty())
                startLine = startLineS.toInt();
            QString code = caps.at(3);
            code = toRunableCode(toMultiLine(code));
            QString content = readTextFileAutoCodec(fileName);
            QStringList lines = content.split("\n", QString::SkipEmptyParts);
            for (int i = startLine; i < lines.size(); i++)
            {
                LiveDanmaku dmk = danmaku;
                dmk.setNumber(i+1);
                dmk.setText(lines.at(i));
                QStringList sl = getEditConditionStringList(code, dmk);
                if (!sl.empty())
                    sendAutoMsg(sl.first(), dmk);
            }
            qInfo() << "遍历文件：" << fileName;
            return true;
        }
    }


    // 文件每一行
    if (msg.contains("CSVEachLine") || msg.contains("csvEachLine"))
    {
        re = RE("(?:CSV|csv)EachLine\\s*\\(\\s*(.+?)\\s*,(?:\\s*(\\d+),)?\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString fileName = toFilePath(caps.at(1));
            QString startLineS = caps.at(2);
            int startLine = 0;
            if (!startLineS.isEmpty())
                startLine = startLineS.toInt();
            QString code = caps.at(3);
            code = toRunableCode(toMultiLine(code));
            QString content = readTextFileAutoCodec(fileName);
            QStringList lines = content.split("\n", QString::SkipEmptyParts);
            for (int i = startLine; i < lines.size(); i++)
            {
                LiveDanmaku dmk = danmaku;
                dmk.setNumber(i+1);
                dmk.setText(lines.at(i));

                QStringList li = lines.at(i).split(QRegExp("\\s*,\\s*"));
                li.insert(0, lines.at(i));
                dmk.setArgs(li);

                QStringList sl = getEditConditionStringList(code, dmk);
                if (!sl.empty())
                    sendAutoMsg(sl.first(), dmk);
            }
            qInfo() << "遍历文件：" << fileName;
            return true;
        }
    }

    // 播放音频文件
    if (msg.contains("playSound"))
    {
        re = RE("playSound\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString path = toFilePath(caps.at(1));
            qInfo() << "执行命令：" << caps;
            QMediaPlayer* player = new QMediaPlayer(this);
            player->setMedia(QUrl::fromLocalFile(path));
            connect(player, &QMediaPlayer::stateChanged, this, [=](QMediaPlayer::State state) {
                if (state == QMediaPlayer::StoppedState)
                {
                    player->deleteLater();
                    triggerCmdEvent("PLAY_SOUND_FINISHED", LiveDanmaku(path));
                }
            });
            player->play();
            qInfo() << "播放音频：" << path;
            return true;
        }
    }

    // 保存到配置
    if (msg.contains("setSetting"))
    {
        re = RE("setSetting\\s*\\(\\s*(\\S+?)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            QString value = caps.at(2);
            qInfo() << "执行命令：" << caps;
            us->setValue(key, value);
            return true;
        }
    }

    // 删除配置
    if (msg.contains("removeSetting"))
    {
        re = RE("removeSetting\\s*\\(\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            qInfo() << "执行命令：" << caps;

            us->remove(key);
            return true;
        }
    }

    // 保存到heaps
    if (msg.contains("setValue"))
    {
        re = RE("setValue\\s*\\(\\s*(\\S+?)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            QString value = caps.at(2);
            qInfo() << "执行命令：" << caps;
            heaps->setValue(key, value);
            return true;
        }
    }

    // 添加值
    if (msg.contains("addValue"))
    {
        re = RE("addValue\\s*\\(\\s*(\\S+?)\\s*,\\s*(-?\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            qint64 value = heaps->value(key).toLongLong();
            qint64 modify = caps.at(2).toLongLong();
            value += modify;
            heaps->setValue(key, value);
            qInfo() << "执行命令：" << caps << key << value;
            return true;
        }
    }

    // 批量修改heaps
    if (msg.contains("setValues"))
    {
        re = RE("setValues\\s*\\(\\s*(\\S+?)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            QString value = caps.at(2);
            qInfo() << "执行命令：" << caps;

            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re) > -1)
                {
                    heaps->setValue(keys.at(i), value);
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 批量添加heaps
    if (msg.contains("addValues"))
    {
        re = RE("addValues\\s*\\(\\s*(\\S+?)\\s*,\\s*(-?\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            qint64 modify = caps.at(2).toLongLong();
            qInfo() << "执行命令：" << caps;

            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re) > -1)
                {
                    heaps->setValue(keys.at(i), heaps->value(keys.at(i)).toLongLong() + modify);
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 按条件批量修改heaps
    if (msg.contains("setValuesIf"))
    {
        re = RE("setValuesIf\\s*\\(\\s*(\\S+?)\\s*,\\s*\\[(.*?)\\]\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            QString VAL_EXP = caps.at(2);
            QString newValue = caps.at(3);
            qInfo() << "执行命令：" << caps;

            // 开始修改
            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            QRegularExpressionMatch match2;
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re, 0, &match2) > -1)
                {
                    QString exp = VAL_EXP;
                    // _VALUE_ 替换为 当前key的值
                    exp.replace("_VALUE_", heaps->value(keys.at(i)).toString());
                    // _$1_ 替换为 match的值
                    if (exp.contains("_$"))
                    {
                        auto caps = match2.capturedTexts();
                        for (int i = 0; i < caps.size(); i++)
                            exp.replace("_$" + snum(i) + "_", caps.at(i));
                    }
                    // 替换获取配置的值 _{}_
                    if (exp.contains("_{"))
                    {
                        QRegularExpression re2("_\\{(.*?)\\}_");
                        while (exp.indexOf(re2, 0, &match2) > -1)
                        {
                            QString _var = match2.captured(0);
                            QString key = match2.captured(1);
                            QVariant var = heaps->value(key);
                            exp.replace(_var, var.toString());
                        }
                    }
                    if (ConditionUtil::judgeCondition(exp))
                    {
                        // 处理 newValue
                        if (newValue.contains("_VALUE_"))
                        {
                            // _VALUE_ 替换为 当前key的值
                            newValue.replace("_VALUE_", heaps->value(keys.at(i)).toString());

                            // 替换计算属性 _[]_
                            if (newValue.contains("_["))
                            {
                                QRegularExpression re2("_\\[(.*?)\\]_");
                                while (newValue.indexOf(re2, 0, &match2) > -1)
                                {
                                    QString _var = match2.captured(0);
                                    QString text = match2.captured(1);
                                    text = snum(ConditionUtil::calcIntExpression(text));
                                    newValue.replace(_var, text); // 默认使用变量类型吧
                                }
                            }
                        }

                        // 真正设置
                        heaps->setValue(keys.at(i), newValue);
                    }
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 按条件批量添加heaps
    if (msg.contains("addValuesIf"))
    {
        re = RE("addValuesIf\\s*\\(\\s*(\\S+?)\\s*,\\s*\\[(.*?)\\]\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            QString VAL_EXP = caps.at(2);
            qint64 modify = caps.at(3).toLongLong();
            qInfo() << "执行命令：" << caps;

            // 开始修改
            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            QRegularExpressionMatch match2;
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re, 0, &match2) > -1)
                {
                    QString exp = VAL_EXP;
                    // _VALUE_ 替换为 当前key的值
                    exp.replace("_VALUE_", heaps->value(keys.at(i)).toString());
                    // _$1_ 替换为 match的值
                    if (exp.contains("_$"))
                    {
                        auto caps = match2.capturedTexts();
                        for (int i = 0; i < caps.size(); i++)
                            exp.replace("_$" + snum(i) + "_", caps.at(i));
                    }
                    // 替换获取配置的值 _{}_
                    if (exp.contains("_{"))
                    {
                        QRegularExpression re2("_\\{(.*?)\\}_");
                        while (exp.indexOf(re2, 0, &match2) > -1)
                        {
                            QString _var = match2.captured(0);
                            QString key = match2.captured(1);
                            QVariant var = heaps->value(key);
                            exp.replace(_var, var.toString());
                        }
                    }

                    if (ConditionUtil::judgeCondition(exp))
                    {
                        heaps->setValue(keys.at(i), heaps->value(keys.at(i)).toLongLong() + modify);
                    }
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 删除heaps
    if (msg.contains("removeValue"))
    {
        re = RE("removeValue\\s*\\(\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            qInfo() << "执行命令：" << caps;

            heaps->remove(key);
            return true;
        }
    }

    // 批量删除heaps
    if (msg.contains("removeValues"))
    {
        re = RE("removeValues\\s*\\(\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            qInfo() << "执行命令：" << caps;

            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re) > -1)
                {
                    heaps->remove(keys.takeAt(i--));
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 按条件批量删除heaps
    if (msg.contains("removeValuesIf"))
    {
        re = RE("removeValuesIf\\s*\\(\\s*(\\S+?)\\s*,\\s*\\[(.*)\\]\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            QString VAL_EXP = caps.at(2);
            qInfo() << "执行命令：" << caps;

            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            QRegularExpressionMatch match2;
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re, 0, &match2) > -1)
                {
                    QString exp = VAL_EXP;
                    // _VALUE_ 替换为 当前key的值
                    exp.replace("_VALUE_", heaps->value(keys.at(i)).toString());
                    // _$1_ 替换为 match的值
                    if (exp.contains("_$"))
                    {
                        auto caps = match2.capturedTexts();
                        for (int i = 0; i < caps.size(); i++)
                            exp.replace("_$" + snum(i) + "_", caps.at(i));
                    }
                    // 替换获取配置的值 _{}_
                    if (exp.contains("_{"))
                    {
                        QRegularExpression re2("_\\{(.*?)\\}_");
                        while (exp.indexOf(re2, 0, &match2) > -1)
                        {
                            QString _var = match2.captured(0);
                            QString key = match2.captured(1);
                            QVariant var = heaps->value(key);
                            exp.replace(_var, var.toString());
                        }
                    }
                    if (ConditionUtil::judgeCondition(exp))
                    {
                        heaps->remove(keys.takeAt(i--));
                    }
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 提升点歌
    if (msg.contains("improveSongOrder") || msg.contains("improveMusic"))
    {
        re = RE("improve(?:SongOrder|Music)\\s*\\(\\s*(.+?)\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString uname = caps.at(1);
            int promote = caps.at(2).toInt();
            qInfo() << "执行命令：" << caps;
            if (musicWindow)
            {
                musicWindow->improveUserSongByOrder(uname, promote);
            }
            else
            {
                localNotify("未开启点歌姬");
                qWarning() << "未开启点歌姬";
            }
            return true;
        }
    }

    // 切歌
    if (msg.contains("cutOrderSong") || msg.contains("cutMusic"))
    {
        // 指定用户昵称的切歌
        re = RE("cut(?:OrderSong|Music)\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString uname = caps.at(1);
            qInfo() << "执行命令：" << caps;
            if (musicWindow)
            {
                if (!musicWindow->cutSongIfUser(uname))
                {
                    const Song &song = musicWindow->getPlayingSong();
                    if (!song.isValid())
                        showError("切歌失败", "未在播放歌曲");
                    else if (song.addBy.isEmpty())
                        showError("切歌失败", "用户不能切手动播放的歌");
                    else
                        showError("切歌失败", "“" + uname + "”无法切“" + song.addBy + "”的歌");
                }
            }
            else
            {
                showError("切歌失败", "未开启点歌姬");
            }
            return true;
        }

        // 强制切歌
        re = RE("cut(?:OrderSong|Music)\\s*\\(\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            if (musicWindow)
            {
                if (!musicWindow->cutSong())
                    showError("切歌失败，未在播放歌曲");
            }
            else
            {
                showError("未开启点歌姬");
            }
            return true;
        }
    }

    // 播放暂停歌曲
    if (msg.contains(QRegExp("(play|pause|toggle)(OrderSong|Music)")))
    {
        re = RE("(play|pause|toggle)(OrderSong|Music)(State)?\\s*\\(\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString action = caps[1];
            qInfo() << "执行命令：" << caps;
            if (musicWindow)
            {
                if (action == "play")
                    musicWindow->play();
                else if (action == "pause")
                    musicWindow->pause();
                else if (action == "toggle")
                    musicWindow->togglePlayState();
            }
            else
            {
                localNotify("未开启点歌姬");
                qWarning() << "未开启点歌姬";
            }
            return true;
        }
    }

    // 提醒框
    if (msg.contains("addMusic") || msg.contains("addOrderSong"))
    {
        re = RE("add(?:OrderSong|Music)\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString song = caps.at(1);
            qInfo() << "执行命令：" << caps;
            if (musicWindow)
            {
                musicWindow->addMusic(song);
            }
            else
            {
                localNotify("未开启点歌姬");
                qWarning() << "未开启点歌姬";
            }
            return true;
        }
    }

    // 提醒框
    if (msg.contains("msgBox") || msg.contains("messageBox"))
    {
        re = RE("(?:msgBox|messageBox)\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = toMultiLine(caps.at(1));
            qInfo() << "执行命令：" << caps;
            QMessageBox::information(this, "神奇弹幕", text);
            return true;
        }
    }

    // 发送长文本
    if (msg.contains("sendLongText"))
    {
        re = RE("sendLongText\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            sendLongText(text);
            return true;
        }
    }

    // 执行自动回复任务
    if (msg.contains("triggerReply"))
    {
        re = RE("triggerReply\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            bool replyed = false;
            for (int i = 0; i < ui->replyListWidget->count(); i++)
            {
                auto rw = static_cast<ReplyWidget*>(ui->replyListWidget->itemWidget(ui->replyListWidget->item(i)));
                if (rw->triggerIfMatch(text, danmaku))
                    replyed = true;
            }
            if (!replyed)
            {
                qWarning() << "未找到合适的回复动作：" << text;
                showError("triggerReply", "未找到合适的回复动作");
            }
            return true;
        }
    }

    // 开关定时任务: enableTimerTask(id, time)
    // time=1开，0关，>1修改时间
    if (msg.contains("enableTimerTask"))
    {
        re = RE("enableTimerTask\\s*\\(\\s*(.+)\\s*,\\s*(-?\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString id = caps.at(1);
            int time = caps.at(2).toInt();
            qInfo() << "执行命令：" << caps;
            bool find = false;
            for (int i = 0; i < ui->taskListWidget->count(); i++)
            {
                auto tw = static_cast<TaskWidget*>(ui->taskListWidget->itemWidget(ui->taskListWidget->item(i)));
                if (!tw->matchId(id))
                    continue;
                if (time == 0) // 切换
                    tw->check->setChecked(!tw->check->isChecked());
                else if (time == 1) // 开
                    tw->check->setChecked(true);
                else if (time == -1) // 关
                    tw->check->setChecked(false);
                else if (time > 1) // 修改时间
                    tw->spin->setValue(time);
                else if (time < -1) // 刷新
                    tw->timer->start();
                find = true;
            }
            if (!find)
                showError("enableTimerTask", "未找到对应ID：" + id);
            return true;
        }
    }

    // 强制AI回复
    if (msg.contains("aiReply") || msg.contains("AIReply"))
    {
        re = RE("(?:ai|AI)Reply\\s*\\(\\s*(.+?)\\s*(?:,\\s*(\\d+))?\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString text = caps.at(1).trimmed();
            if (text.isEmpty())
                return true;
            int maxLen = ui->danmuLongestSpin->value(); // 默认只有一条弹幕的长度
            if (caps.size() > 2 && !caps.at(2).isEmpty()
                    && caps.at(2).toInt() > 0)
                maxLen = caps.at(2).toInt();
            if (caps.size() > 3 && !caps.at(3).isEmpty())
            {
                // 触发事件
                TxNlp::instance()->chat(text, [=](QString s){
                    QJsonObject js;
                    js.insert("reply", s);
                    LiveDanmaku dm = danmaku;
                    dm.with(js);
                    triggerCmdEvent(caps.at(3), danmaku);
                }, maxLen);
            }
            else
            {
                // 直接发送弹幕
                TxNlp::instance()->chat(text, [=](QString s){
                    sendLongText(s);
                }, maxLen);
            }
            return true;
        }
    }

    if (msg.contains("aiChat") || msg.contains("AIChat"))
    {
        re = RE("(?:ai|AI)Chat\\s*\\(\\s*\"(.+?)\"\\s*,\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("(?:ai|AI)Chat\\s*\\(\\s*(.+?)\\s*,\\s*(.+)\\s*\\)");
            msg.indexOf(re, 0, &match);
        }

        if (match.isValid())
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString text = caps.at(1).trimmed();
            if (text.isEmpty())
                return true;
            QString code = caps.at(2);
            code = toRunableCode(toMultiLine(code));
            TxNlp::instance()->chat(text, [=](QString result) {
                LiveDanmaku dmk = danmaku;
                dmk.setText(result);
                QStringList sl = getEditConditionStringList(code, dmk);
                if (!sl.empty())
                    sendAutoMsg(sl.first(), dmk);
            });
            return true;
        }
    }

    // 忽略自动欢迎
    if (msg.contains("ignoreWelcome"))
    {
        re = RE("ignoreWelcome\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            qInfo() << "执行命令：" << caps;

            if (!us->notWelcomeUsers.contains(uid))
            {
                us->notWelcomeUsers.append(uid);

                QStringList ress;
                foreach (qint64 uid, us->notWelcomeUsers)
                    ress << QString::number(uid);
                us->setValue("danmaku/notWelcomeUsers", ress.join(";"));
            }

            return true;
        }
    }

    // 启用欢迎
    if (msg.contains("enableWelcome"))
    {
        re = RE("enableWelcome\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            qInfo() << "执行命令：" << caps;

            if (us->notWelcomeUsers.contains(uid))
            {
                us->notWelcomeUsers.removeOne(uid);

                QStringList ress;
                foreach (qint64 uid, us->notWelcomeUsers)
                    ress << QString::number(uid);
                us->setValue("danmaku/notWelcomeUsers", ress.join(";"));
            }

            return true;
        }
    }

    // 设置专属昵称
    if (msg.contains("setNickname") || msg.contains("setLocalName"))
    {
        re = RE("set(?:Nickname|LocalName)\\s*\\(\\s*(\\d+)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            QString name = caps.at(2);
            qInfo() << "执行命令：" << caps;
            if (uid == 0)
            {
                showError("setLocalName失败", "未找到UID:" + caps.at(1));
            }
            else if (name.isEmpty()) // 移除
            {
                if (us->localNicknames.contains(uid))
                    us->localNicknames.remove(uid);
            }
            else // 添加
            {
                us->localNicknames[uid] = name;

                QStringList ress;
                auto it = us->localNicknames.begin();
                while (it != us->localNicknames.end())
                {
                    ress << QString("%1=>%2").arg(it.key()).arg(it.value());
                    it++;
                }
                us->setValue("danmaku/localNicknames", ress.join(";"));
            }
            return true;
        }
    }

    // 开启大乱斗
    if (msg.contains("joinBattle"))
    {
        re = RE("joinBattle\\s*\\(\\s*([12])\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            int type = caps.at(1).toInt();
            qInfo() << "执行命令：" << caps;
            joinBattle(type);
            return true;
        }
    }

    // 自定义事件
    if (msg.contains("triggerEvent") || msg.contains("emitEvent"))
    {
        re = RE("(?:trigger|emit)Event\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            triggerCmdEvent(text, danmaku);
            return true;
        }
    }

    // 点歌
    if (msg.contains("orderSong"))
    {
        re = RE("orderSong\\s*\\(\\s*(.+)\\s*,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            QString uname = caps.at(2);
            qInfo() << "执行命令：" << caps;
            if (!musicWindow)
                on_actionShow_Order_Player_Window_triggered();
            musicWindow->slotSearchAndAutoAppend(text, uname);
            return true;
        }
    }

    // 模拟快捷键
    if (msg.contains("simulateKeys"))
    {
        re = RE("simulateKeys\\s*\\((.+)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            simulateKeys(text);
            return true;
        }
    }

    if (msg.contains("simulatePressKeys"))
    {
        re = RE("simulatePressKeys\\s*\\((.+)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            simulateKeys(text, true, false);
            return true;
        }
    }

    if (msg.contains("simulateReleaseKeys"))
    {
        re = RE("simulateReleaseKeys\\s*\\((.+)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qInfo() << "执行命令：" << caps;
            simulateKeys(text, false, true);
            return true;
        }
    }

    // 模拟鼠标点击
    if (msg.contains("simulateClick"))
    {
        re = RE("simulateClick\\s*\\(\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            simulateClick();
            return true;
        }

        // 点击绝对位置
        re = RE("simulateClick\\s*\\(\\s*([-\\d]+)\\s*,\\s*([-\\d]+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            unsigned long x = caps.at(1).toLong();
            unsigned long y = caps.at(2).toLong();
            qInfo() << "执行命令：" << caps;
            moveMouseTo(x, y);
            simulateClick();
            return true;
        }
    }

    // 指定按键的模拟
    if (msg.contains("simulateClickButton"))
    {
#ifdef Q_OS_WIN
        auto getFlags = [=](QString param) -> DWORD {
            if (param == "left")
                return MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
            else if (param == "right")
                return MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP;
            else if (param == "middle")
                return MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP;
            else if (param == "x")
                return MOUSEEVENTF_XDOWN | MOUSEEVENTF_XUP;
            else if (param.indexOf(QRegExp("^\\d+$")) > -1)
                return param.toUShort();
            else
            {
                qWarning() << "无法识别的鼠标按键：" << param;
                return 0;
            }
        };

        // 点击当前位置
        re = RE("simulateClickButton\\s*\\(\\s*(\\S*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString param = caps[1].toLower();
            qInfo() << "执行命令：" << caps;
            DWORD flag = getFlags(param);
            if (flag == 0)
                return true;

            simulateClickButton(flag);
            return true;
        }

        // 点击绝对位置
        re = RE("simulateClickButton\\s*\\(\\s*(\\S+)\\s*,\\s*([-\\d]+)\\s*,\\s*([-\\d]+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString param = caps[1].toLower();
            unsigned long x = caps.at(2).toLong();
            unsigned long y = caps.at(3).toLong();
            qInfo() << "执行命令：" << caps;
            DWORD flag = getFlags(param);
            if (flag == 0)
                return true;
            moveMouseTo(x, y);
            simulateClickButton(flag);
            return true;
        }
#endif
    }

    // 移动鼠标（相对现在位置）
    if (msg.contains("moveMouse"))
    {
        re = RE("moveMouse\\s*\\(\\s*([-\\d]+)\\s*,\\s*([-\\d]+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            unsigned long dx = caps.at(1).toLong();
            unsigned long dy = caps.at(2).toLong();
            qInfo() << "执行命令：" << caps;
            moveMouse(dx, dy);
            return true;
        }
    }

    // 移动鼠标到绝对位置
    if (msg.contains("moveMouseTo"))
    {
        re = RE("moveMouseTo\\s*\\(\\s*([-\\d]+)\\s*,\\s*([-\\d]+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            unsigned long dx = caps.at(1).toLong();
            unsigned long dy = caps.at(2).toLong();
            qInfo() << "执行命令：" << caps;
            moveMouseTo(dx, dy);
            return true;
        }
    }

    // 执行脚本
    if (msg.contains("execScript"))
    {
        re = RE("execScript\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            QString path;
            if (isFileExist(path = rt->dataPath + "control/" + text + ".bat"))
            {
                qInfo() << "执行bat脚本：" << path;
                QProcess p(nullptr);
                p.start(path);
                if (!p.waitForFinished())
                    qWarning() << "执行bat脚本失败：" << path << p.errorString();
            }
            else if (isFileExist(path = rt->dataPath + "control/" + text + ".vbs"))
            {
                qInfo() << "执行vbs脚本：" << path;
                QDesktopServices::openUrl("file:///" + path);
            }
            else if (isFileExist(path = text + ".bat"))
            {
                qInfo() << "执行bat脚本：" << path;
                QProcess p(nullptr);
                p.start(path);
                if (!p.waitForFinished())
                    qWarning() << "执行bat脚本失败：" << path << p.errorString();
            }
            else if (isFileExist(path = text + ".vbs"))
            {
                qInfo() << "执行vbs脚本：" << path;
                QDesktopServices::openUrl("file:///" + path);
            }
            else if (isFileExist(path = text))
            {
                qInfo() << "执行脚本：" << path;
                QDesktopServices::openUrl("file:///" + path);
            }
            else
            {
                qWarning() << "脚本不存在：" << text;
            }
            return true;
        }
    }

    // 添加违禁词（到指定锚点）
    if (msg.contains("addBannedWord"))
    {
        re = RE("addBannedWord\\s*\\(\\s*(.+)\\s*,\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QString word = caps.at(1);
            QString anchor = caps.at(2);
            addBannedWord(word, anchor);
            return true;
        }
    }

    // 列表值
    // showValueTable(title, loop-key, title1:key1, title2:key2...)
    // 示例：showValueTable(title, integral_(\d+), ID:"_ID_", 昵称:name__ID_, 积分:integral__ID_)
    if (msg.contains("showValueTable"))
    {
        re = RE("showValueTable\\s*\\((.+)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList tableFileds = match.captured(1).trimmed().split(QRegularExpression("\\s*,\\s*"));
            QString caption, loopKeyStr;
            if (tableFileds.size() >= 2)
                caption = tableFileds.takeFirst();
            if (tableFileds.size() >= 1)
                loopKeyStr = tableFileds.takeFirst();
            if (loopKeyStr.trimmed().isEmpty()) // 关键词是空的，不知道要干嘛
                return true;
            if (!loopKeyStr.contains("/"))
                loopKeyStr = "heaps/" + loopKeyStr;
            QSettings* sts = heaps;
            if (loopKeyStr.startsWith(COUNTS_PREFIX))
            {
                loopKeyStr.remove(0, COUNTS_PREFIX.length());
                sts = us->danmakuCounts;
            }
            else if (loopKeyStr.startsWith(SETTINGS_PREFIX))
            {
                loopKeyStr.remove(0, SETTINGS_PREFIX.length());
                sts = us;
            }

            auto viewer = new VariantViewer(caption, sts, loopKeyStr, tableFileds, us->danmakuCounts, heaps, this);
            viewer->setGeometry(this->geometry());
            viewer->show();
            return true;
        }
    }

    if (msg.contains("showCSV"))
    {
        re = RE("showCSV\\s*\\((.*)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QString path = match.captured(1);
            auto showCSV = [=](QString path){
                CSVViewer * view = new CSVViewer(path, this);
                view->show();
            };

            if (isFileExist(path))
            {
                showCSV(path);
            }
            else if (isFileExist(rt->dataPath + path))
            {
                showCSV(rt->dataPath + path);
            }
            return true;
        }
    }

    if (msg.contains("execTouta"))
    {
        re = RE("execTouta\\s*\\(\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            if (pking)
                execTouta();
            else
                qWarning() << "不在PK中，无法偷塔";
            return true;
        }
    }

    if (msg.contains("setToutaMaxGold"))
    {
        re = RE("setToutaMaxGold\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            int val = caps.at(1).toInt();
            us->setValue("pk/maxGold", pkMaxGold = val);
            return true;
        }
    }

    if (msg.contains("copyText"))
    {
        re = RE("copyText\\s*\\((.+?)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            QApplication::clipboard()->setText(toMultiLine(caps.at(1)));
            return true;
        }
    }

    if (msg.contains("setRoomTitle"))
    {
        re = RE("setRoomTitle\\s*\\((.+?)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            myLiveSetTitle(caps.at(1));
            return true;
        }
    }

    if (msg.contains("setRoomCover"))
    {
        re = RE("setRoomCover\\s*\\((.+?)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            myLiveSetCover(caps.at(1));
            return true;
        }
    }

    if (msg.contains("setLocalMode"))
    {
        re = RE("setLocalMode\\s*\\((.+?)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString val = caps[1];
            qInfo() << "执行命令：" << caps;
            if (val == "" || val == "0" || val.toLower() == "false")
            {
                // true -> false
                ui->actionLocal_Mode->setChecked(false);
            }
            else
            {
                // false -> true
                ui->actionLocal_Mode->setChecked(true);
            }
            on_actionLocal_Mode_triggered();
            return true;
        }
    }

    if (msg.contains("reconnectRoom"))
    {
        re = RE("reconnectRoom\\s*\\(\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qInfo() << "执行命令：" << caps;
            startConnectRoom();
            return true;
        }
    }

    // 执行数据库
    if (msg.contains("sqlExec") || msg.contains("SQLExec"))
    {
        re = RE("(?:sql|SQL)Exec\\s*\\((.+?)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString sql = caps[1];
            sql = toMultiLine(sql);
            qInfo() << "执行命令：" << caps;
            if (sqlService.exec(sql))
                localNotify("数据库执行成功：" + sql);
            return true;
        }
    }

    // 显示数据库查询结果
    if (msg.contains("sqlQuery") || msg.contains("SQLQuery"))
    {
        re = RE("(?:sql|SQL)Query\\s*\\((.+?)\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString sql = caps[1];
            sql = toMultiLine(sql);
            qInfo() << "执行命令：" << caps;
            showSqlQueryResult(sql);
            return true;
        }
    }

    return false;
}

/**
 * 获取第一个符合条件的回复的文本
 * 会返回所有文本，如果有换行符，则会以脚本的 \n（实际上是\\n）来保存
 * 如果有命令，会继续保持文本形式，而不会触发
 * 仅判断启用的
 */
QString MainWindow::getReplyExecutionResult(QString key, const LiveDanmaku& danmaku)
{
    for (int row = 0; row < ui->replyListWidget->count(); row++)
    {
        auto rowItem = ui->replyListWidget->item(row);
        auto widget = ui->replyListWidget->itemWidget(rowItem);
        if (!widget)
            continue;
        auto replyWidget = static_cast<ReplyWidget*>(widget);
        if (replyWidget->isEnabled() && key.contains(QRegularExpression(replyWidget->title())))
        {
            QString filterText = replyWidget->body();
            QStringList msgs = getEditConditionStringList(filterText, danmaku);
            return getExecutionResult(msgs, danmaku);
        }
    }
    return "";
}

QString MainWindow::getEventExecutionResult(QString key, const LiveDanmaku &danmaku)
{
    for (int row = 0; row < ui->eventListWidget->count(); row++)
    {
        auto rowItem = ui->eventListWidget->item(row);
        auto widget = ui->eventListWidget->itemWidget(rowItem);
        if (!widget)
            continue;
        auto eventWidget = static_cast<EventWidget*>(widget);
        if (eventWidget->isEnabled() && eventWidget->title() == key)
        {
            QString filterText = eventWidget->body();
            QStringList msgs = getEditConditionStringList(filterText, danmaku);
            return getExecutionResult(msgs, danmaku);
        }
    }
    return "";
}

/**
 * 多个可执行序列
 * 选其中一个序列来执行与回复
 */
QString MainWindow::getExecutionResult(QStringList& msgs, const LiveDanmaku &_danmaku)
{
    if (!msgs.size())
        return "";

    // 随机获取其中的一条
    int r = qrand() % msgs.size();
    QStringList dms = msgs.at(r).split("\\n");

    // 尝试执行
    CmdResponse res = NullRes;
    int resVal = 0;
    LiveDanmaku& danmaku = const_cast<LiveDanmaku&>(_danmaku);
    for (int i = 0; i < dms.size(); i++)
    {
        const QString& dm = dms.at(i);
        if (execFunc(dm, danmaku, res, resVal))
        {
            if (res == AbortRes)
                return "";
            else if (res == DelayRes)
                ;
            dms.removeAt(i--);
        }
    }

    // 返回的弹幕内容
    return toSingleLine(dms.join("\\n"));
}

void MainWindow::simulateKeys(QString seq, bool press, bool release)
{
    if (seq.isEmpty())
        return ;

    // 模拟点击右键
    // mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    // mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    // keybd_event(VK_CONTROL, (BYTE) 0, 0, 0);
    // keybd_event('P', (BYTE)0, 0, 0);
    // keybd_event('P', (BYTE)0, KEYEVENTF_KEYUP, 0);
    // keybd_event(VK_CONTROL, (BYTE)0, KEYEVENTF_KEYUP, 0);
#if defined(Q_OS_WIN)
    // 字符串转KEY
    QList<int>keySeq;
    QStringList keyStrs = seq.toLower().split("+", QString::SkipEmptyParts);

    // 先判断修饰键
    /*if (keyStrs.contains("ctrl"))
        keySeq.append(VK_CONTROL);
    if (keyStrs.contains("shift"))
        keySeq.append(VK_SHIFT);
    if (keyStrs.contains("alt"))
        keySeq.append(VK_MENU);
    keyStrs.removeOne("ctrl");
    keyStrs.removeOne("shift");
    keyStrs.removeOne("alt");*/

    // 其他键
    for (int i = 0; i < keyStrs.size(); i++)
    {
        QString ch = keyStrs.at(i);
        if (ch == "ctrl" || ch == "control")
            keySeq.append(VK_CONTROL);
        else if (ch == "shift")
            keySeq.append(VK_SHIFT);
        else if (ch == "alt")
            keySeq.append(VK_MENU);
        /* else if (ch >= "0" && ch <= "9")
            keySeq.append(0x30 + ch.toInt());
        else if (ch >= "a" && ch <= "z")
            keySeq.append(0x41 + ch.at(0).toLatin1() - 'a'); */
        else
        {
            char c;
            // 特判名字
            if (ch == "add")
                c = '+';
            else if (ch == "space")
                c = ' ';
            else
                c = ch.at(0).toLatin1();

            DWORD sc = OemKeyScan(c);
            // DWORD shift = sc >> 16; // 判断有没有按下shift键（这里当做没有）
            unsigned char vkey = MapVirtualKey(sc & 0xffff, 1);
            keySeq.append(vkey);
        }
    }

    // 模拟按下（全部）
    if (press)
    {
        for (int i = 0; i < keySeq.size(); i++)
            keybd_event(keySeq.at(i), (BYTE) 0, 0, 0);
    }

    // 模拟松开（全部）
    if (release)
    {
        for (int i = 0; i < keySeq.size(); i++)
            keybd_event(keySeq.at(i), (BYTE) 0, KEYEVENTF_KEYUP, 0);
    }
#endif
}

/// 模拟鼠标点击（当前位置）
/// 参考资料：https://blog.csdn.net/qq_34106574/article/details/89639503
void MainWindow::simulateClick()
{
    // mouse_event(dwFlags, dx, dy, dwData, dwExtraInfo)
#ifdef Q_OS_WIN
    mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
#else
    qWarning() << "不支持模拟鼠标点击";
#endif
}

void MainWindow::simulateClickButton(qint64 keys)
{
#ifdef Q_OS_WIN
    mouse_event((DWORD)keys, 0, 0, 0, 0);
#else
    qWarning() << "不支持模拟鼠标点击";
#endif
}

/// 移动鼠标位置
void MainWindow::moveMouse(unsigned long dx, unsigned long dy)
{
#ifdef Q_OS_WIN
    mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
#else
    qWarning() << "不支持模拟鼠标点击";
#endif
}

/// 移动鼠标位置
void MainWindow::moveMouseTo(unsigned long tx, unsigned long ty)
{
#ifdef Q_OS_WIN
    // QPoint pos = QCursor::pos();
    // qInfo() << "鼠标移动距离：" << QPoint(tx, ty) << pos << QPoint(tx - pos.x(), ty - pos.y());
    // mouse_event(MOUSEEVENTF_MOVE, tx - pos.x(), ty - pos.y(), 0, 0); // 这个计算出来的位置不对啊！
    SetCursorPos(tx, ty); // 这个必须要在本程序窗口的区域内才有效
#else
    qWarning() << "不支持模拟鼠标点击";
#endif
}

QStringList MainWindow::splitLongDanmu(QString text) const
{
    QStringList sl;
    int len = text.length();
    const int maxOne = danmuLongest;
    int count = (len + maxOne - 1) / maxOne;
    for (int i = 0; i < count; i++)
    {
        sl << text.mid(i * maxOne, maxOne);
    }
    return sl;
}

void MainWindow::sendLongText(QString text)
{
    if (text.contains("%n%"))
    {
        text.replace("%n%", "\n");
        for (auto s : text.split("\n", QString::SkipEmptyParts))
        {
            sendLongText(s);
        }
        return ;
    }
    sendAutoMsg(splitLongDanmu(text).join("\\n"), LiveDanmaku());
}

void MainWindow::restoreCustomVariant(QString text)
{
    us->customVariant.clear();
    QStringList sl = text.split("\n", QString::SkipEmptyParts);
    bool settedUpname = true;
    foreach (QString s, sl)
    {
        QRegularExpression re("^\\s*(\\S+?)\\s*=\\s?(.*)$");
        QRegularExpressionMatch match;
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            us->customVariant.append(QPair<QString, QString>(key, val));
            if ((key == "%upname%") && val.trimmed().isEmpty())
            {
                settedUpname = false;
            }
            else
            {
                settedUpname = true;
            }
        }
        else
            qCritical() << "自定义变量读取失败：" << s;
    }
    customVarsButton->setBgColor(settedUpname ? Qt::transparent : Qt::white);
}

QString MainWindow::saveCustomVariant()
{
    QStringList sl;
    for (auto it = us->customVariant.begin(); it != us->customVariant.end(); ++it)
    {
        sl << it->first + " = " + it->second;
    }
    return sl.join("\n");
}

void MainWindow::restoreVariantTranslation()
{
    us->variantTranslation.clear();
    QStringList allVariants;

    // 变量
    QString text = readTextFile(":/documents/translation_variables");
    QStringList sl = text.split("\n", QString::SkipEmptyParts);
    QRegularExpression re("^\\s*(\\S+)\\s*=\\s?(.*)$");
    QRegularExpressionMatch match;
    foreach (QString s, sl)
    {
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            key = "%" + key + "%";
            val = "%" + val + "%";
            us->variantTranslation.append(QPair<QString, QString>(key, val));
            allVariants.append(key);
            allVariants.append(val);
        }
        else
            qCritical() << "多语言翻译读取失败：" << s;
    }

    // 方法
    text = readTextFile(":/documents/translation_methods");
    sl = text.split("\n", QString::SkipEmptyParts);
    re = QRegularExpression("^\\s*(\\S+)\\s*=\\s?(.*)$");
    foreach (QString s, sl)
    {
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            key = ">" + key + "(";
            val = ">" + val + "(";
            us->variantTranslation.append(QPair<QString, QString>(key, val));
            allVariants.append(key);
            allVariants.append(val);
        }
        else
            qCritical() << "多语言翻译读取失败：" << s;
    }

    // 函数
    text = readTextFile(":/documents/translation_functions");
    sl = text.split("\n", QString::SkipEmptyParts);
    re = QRegularExpression("^\\s*(\\S+)\\s*=\\s?(.*)$");
    foreach (QString s, sl)
    {
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            key = "%>" + key + "(";
            val = "%>" + val + "(";
            us->variantTranslation.append(QPair<QString, QString>(key, val));
            allVariants.append(key);
            allVariants.append(val);
        }
        else
            qCritical() << "多语言翻译读取失败：" << s;
    }

    // 添加自定义关键词
    allVariants.append(">reject(");
    allVariants.append("%up_name%");
    ConditionEditor::allCompletes = allVariants;
}

void MainWindow::restoreReplaceVariant(QString text)
{
    us->replaceVariant.clear();
    QStringList sl = text.split("\n", QString::SkipEmptyParts);
    foreach (QString s, sl)
    {
        QRegularExpression re("^\\s*(\\S+?)\\s*=\\s?(.*)$");
        QRegularExpressionMatch match;
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            us->replaceVariant.append(QPair<QString, QString>(key, val));
        }
        else
            qCritical() << "替换变量读取失败：" << s;
    }
}

QString MainWindow::saveReplaceVariant()
{
    QStringList sl;
    for (auto it = us->replaceVariant.begin(); it != us->replaceVariant.end(); ++it)
    {
        sl << it->first + " = " + it->second;
    }
    return sl.join("\n");
}

void MainWindow::savePlayingSong()
{
    Song song = musicWindow ? musicWindow->getPlayingSong() : Song();
    QString format = ui->playingSongToFileFormatEdit->text();
    QString text;
    if (song.isValid())
    {
        text = format;
        text = text.replace("{歌名}", song.name)
                .replace("{歌手}", song.artistNames)
                .replace("{用户}", song.addBy)
                .replace("{时长}", snum(song.duration/60) + ":" + snum(song.duration%60))
                .replace("{专辑}", song.album.name);
    }
    else
    {
        text = " ";
    }

    // 获取路径
    QDir dir(wwwDir.absoluteFilePath("music"));
    dir.mkpath(dir.absolutePath());

    // 保存名字到文件
    QFile file(dir.absoluteFilePath("playing.txt"));
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!externFileCodec.isEmpty())
        stream.setCodec(externFileCodec.toUtf8());
    stream << text;
    file.flush();
    file.close();

    // 保存封面
    QPixmap cover = musicWindow->getCurrentSongCover();
    cover.save(dir.absoluteFilePath("cover.jpg"));
}

void MainWindow::saveOrderSongs(const SongList &songs)
{
    int count = qMin(songs.size(), ui->orderSongsToFileMaxSpin->value());
    QString format = ui->orderSongsToFileFormatEdit->text();

    Song currentSong = musicWindow ? musicWindow->getPlayingSong() : Song();

    // 组合成长文本
    QStringList sl;
    for (int i = 0; i < count; i++)
    {
        Song song = songs.at(i);
        QString text = format;
        text = text.replace("{序号}", snum(i+1))
                .replace("{歌名}", song.name)
                .replace("{歌手}", song.artistNames)
                .replace("{用户}", song.addBy)
                .replace("{时长}", snum(song.duration/60) + ":" + snum(song.duration%60))
                .replace("{专辑}", song.album.name)
                .replace("{当前歌名}", currentSong.name)
                .replace("{当前歌手}", currentSong.artistNames)
                .replace("{当前用户}", currentSong.addBy);
        sl.append(text);
    }
    if (!sl.size()) // 直播姬会跳过空文本文件，所以需要设置个空格
        sl.append(" ");

    // 获取路径
    QDir dir(wwwDir.absoluteFilePath("music"));
    dir.mkpath(dir.absolutePath());

    // 保存到文件
    QFile file(dir.absoluteFilePath("songs.txt"));
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!externFileCodec.isEmpty())
        stream.setCodec(externFileCodec.toUtf8());
    stream << sl.join("\n");
    file.flush();
    file.close();
}

void MainWindow::saveSongLyrics()
{
    QStringList lyrics = musicWindow->getSongLyrics(ui->songLyricsToFileMaxSpin->value());
    if (!lyrics.size()) // 直播姬会跳过空文本文件，所以需要设置个空格
        lyrics.append(" ");

    // 获取路径
    QDir dir(wwwDir.absoluteFilePath("music"));
    dir.mkpath(dir.absolutePath());

    // 保存到文件
    QFile file(dir.absoluteFilePath("lyrics.txt"));
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!externFileCodec.isEmpty())
        stream.setCodec(externFileCodec.toUtf8());
    stream << lyrics.join("\n");
    file.flush();
    file.close();
}

void MainWindow::slotBinaryMessageReceived(const QByteArray &message)
{
    int operation = ((uchar)message[8] << 24)
            + ((uchar)message[9] << 16)
            + ((uchar)message[10] << 8)
            + ((uchar)message[11]);
    QByteArray body = message.right(message.length() - 16);
    SOCKET_DEB << "操作码=" << operation << "  大小=" << body.size() << "  正文=" << (body.left(1000)) << "...";

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(body, &error);
    QJsonObject json;
    if (error.error == QJsonParseError::NoError)
        json = document.object();

    if (operation == OP_AUTH_REPLY) // 认证包回复
    {
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("认证出错");
        }
    }
    else if (operation == OP_HEARTBEAT_REPLY) // 心跳包回复（人气值）
    {
        qint32 popularity = ((uchar)body[0] << 24)
                + ((uchar)body[1] << 16)
                + ((uchar)body[2] << 8)
                + (uchar)body[3];
        SOCKET_DEB << "人气值=" << popularity;
        this->popularVal = ac->currentPopul = popularity;
        if (isLiving())
            ui->popularityLabel->setText(QString::number(popularity));
    }
    else if (operation == OP_SEND_MSG_REPLY) // 普通包
    {
        QString cmd;
        if (!json.isEmpty())
        {
            cmd = json.value("cmd").toString();

            if (cmd == "STOP_LIVE_ROOM_LIST" || cmd == "NOTICE_MSG")
                return ;

            qInfo() << "普通CMD：" << cmd;
            SOCKET_INF << json;
        }

        if (cmd == "NOTICE_MSG") // 全站广播（不用管）
        {

        }
        else if (cmd == "ROOM_RANK")
        {
            /*{
                "cmd": "ROOM_RANK",
                "data": {
                    "color": "#FB7299",
                    "h5_url": "https://live.bilibili.com/p/html/live-app-rankcurrent/index.html?is_live_half_webview=1&hybrid_half_ui=1,5,85p,70p,FFE293,0,30,100,10;2,2,320,100p,FFE293,0,30,100,0;4,2,320,100p,FFE293,0,30,100,0;6,5,65p,60p,FFE293,0,30,100,10;5,5,55p,60p,FFE293,0,30,100,10;3,5,85p,70p,FFE293,0,30,100,10;7,5,65p,60p,FFE293,0,30,100,10;&anchor_uid=688893202&rank_type=master_realtime_area_hour&area_hour=1&area_v2_id=145&area_v2_parent_id=1",
                    "rank_desc": "娱乐小时榜 7",
                    "roomid": 22532956,
                    "timestamp": 1605749940,
                    "web_url": "https://live.bilibili.com/blackboard/room-current-rank.html?rank_type=master_realtime_area_hour&area_hour=1&area_v2_id=145&area_v2_parent_id=1"
                }
            }*/
            QJsonObject data = json.value("data").toObject();
            QString color = data.value("color").toString();
            QString desc = data.value("rank_desc").toString();
            ui->roomRankLabel->setStyleSheet("color: " + color + ";");
            ui->roomRankLabel->setText(desc);
            ui->roomRankLabel->setToolTip(QDateTime::currentDateTime().toString("更新时间：hh:mm:ss"));
            if (desc != ui->roomRankLabel->text()) // 排名有更新
                localNotify("当前排名：" + desc);

            triggerCmdEvent(cmd, LiveDanmaku().with(data));
        }
        else if (handlePK(json))
        {
        }
        else // 压缩弹幕消息
        {
            short protover = (message[6]<<8) + message[7];
            SOCKET_INF << "协议版本：" << protover;
            if (protover == 2) // 默认协议版本，zlib解压
            {
                slotUncompressBytes(body);
            }
            else if (protover == 0)
            {
                QJsonDocument document = QJsonDocument::fromJson(body, &error);
                if (error.error != QJsonParseError::NoError)
                {
                    qCritical() << s8("body转json出错：") << error.errorString();
                    return ;
                }
                QJsonObject json = document.object();
                QString cmd = json.value("cmd").toString();

                if (saveToSqlite && saveCmdToSqlite)
                {
                    sqlService.insertCmd(cmd, body);
                }

                if (cmd == "STOP_LIVE_ROOM_LIST" || cmd == "WIDGET_BANNER")
                    return ;

                qInfo() << ">消息命令UNZC：" << cmd;

                if (cmd == "ROOM_RANK")
                {
                }
                else if (cmd == "ROOM_REAL_TIME_MESSAGE_UPDATE") // 实时信息改变
                {
                    // {"cmd":"ROOM_REAL_TIME_MESSAGE_UPDATE","data":{"roomid":22532956,"fans":1022,"red_notice":-1,"fans_club":50}}
                    QJsonObject data = json.value("data").toObject();
                    int fans = data.value("fans").toInt();
                    int fans_club = data.value("fans_club").toInt();
                    int delta_fans = 0, delta_club = 0;
                    if (ac->currentFans || ac->currentFansClub)
                    {
                        delta_fans = fans - ac->currentFans;
                        delta_club = fans_club - ac->currentFansClub;
                    }
                    ac->currentFans = fans;
                    ac->currentFansClub = fans_club;
                    qInfo() << s8("粉丝数量：") << fans << s8("  粉丝团：") << fans_club;
                    // appendNewLiveDanmaku(LiveDanmaku(fans, fans_club, delta_fans, delta_club));

                    dailyNewFans += delta_fans;
                    if (dailySettings)
                    {
                        dailySettings->setValue("new_fans", dailyNewFans);
                        dailySettings->setValue("total_fans", ac->currentFans);
                    }

//                    if (delta_fans) // 如果有变动，实时更新
//                        getFansAndUpdate();
                    ui->fansCountLabel->setText(snum(fans));
                }
                else if (cmd == "WIDGET_BANNER") // 无关的横幅广播
                {}
                else if (cmd == "HOT_RANK_CHANGED") // 热门榜
                {
                    /*{
                        "cmd": "HOT_RANK_CHANGED",
                        "data": {
                            "rank": 14,
                            "trend": 2, // 趋势：1上升，2下降
                            "countdown": 1705,
                            "timestamp": 1610168495,
                            "web_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=2\\u0026area_id=1",
                            "live_url": "……（太长了）",
                            "blink_url": "……（太长了）",
                            "live_link_url": "……（太长了）",
                            "pc_link_url": "……（太长了）",
                            "icon": "https://i0.hdslb.com/bfs/live/3f833451003cca16a284119b8174227808d8f936.png",
                            "area_name": "娱乐"
                        }
                    }*/
                    QJsonObject data = json.value("data").toObject();
                    int rank = data.value("rank").toInt();
                    int trend = data.value("trend").toInt(); // 趋势：1上升，2下降
                    QString area_name = data.value("area_name").toString();
                    if (area_name.endsWith("榜"))
                        area_name.replace(area_name.length()-1, 1, "");
                    QString msg = QString("热门榜 " + area_name + "榜 排名：" + snum(rank) + " " + (trend == 1 ? "↑" : "↓"));
                    ui->roomRankLabel->setText(snum(rank));
                    ui->roomRankTextLabel->setText(area_name + "榜");
                    ui->roomRankLabel->setToolTip(msg);
                }
                else if (cmd == "HOT_RANK_SETTLEMENT")
                {
                    /*{
                        "cmd": "HOT_RANK_SETTLEMENT",
                        "data": {
                            "rank": 9,
                            "uname": "xxx",
                            "face": "http://i2.hdslb.com/bfs/face/17f1f3994cb4b2bba97f1557ffc7eb34a05e119b.jpg",
                            "timestamp": 1610173800,
                            "icon": "https://i0.hdslb.com/bfs/live/3f833451003cca16a284119b8174227808d8f936.png",
                            "area_name": "娱乐",
                            "url": "https://live.bilibili.com/p/html/live-app-hotrank/result.html?is_live_half_webview=1\\u0026hybrid_half_ui=1,5,250,200,f4eefa,0,30,0,0,0;2,5,250,200,f4eefa,0,30,0,0,0;3,5,250,200,f4eefa,0,30,0,0,0;4,5,250,200,f4eefa,0,30,0,0,0;5,5,250,200,f4eefa,0,30,0,0,0;6,5,250,200,f4eefa,0,30,0,0,0;7,5,250,200,f4eefa,0,30,0,0,0;8,5,250,200,f4eefa,0,30,0,0,0\\u0026areaId=1\\u0026cache_key=4417cab3fa8b15ad1b250ee29fd91c52",
                            "cache_key": "4417cab3fa8b15ad1b250ee29fd91c52",
                            "dm_msg": "恭喜主播 \\u003c% xxx %\\u003e 荣登限时热门榜娱乐榜top9! 即将获得热门流量推荐哦！"
                        }
                    }*/
                    QJsonObject data = json.value("data").toObject();
                    int rank = data.value("rank").toInt();
                    QString uname = data.value("uname").toString();
                    QString area_name = data.value("area_name").toString();
                    QString msg = QString("恭喜荣登热门榜" + area_name + "榜 top" + snum(rank) + "!");
                    // 还没想好这个分区榜要放到总榜还是小时榜里？
                    ui->roomRankLabel->setText(snum(rank));
                    ui->roomRankTextLabel->setText(area_name + "榜");
                    qInfo() << "rank:" << msg;
                    triggerCmdEvent("HOT_RANK", LiveDanmaku(area_name + "榜 top" + snum(rank)).with(data), true);
                    localNotify(msg);
                }
                else if (cmd == "HOT_RANK_SETTLEMENT_V2")
                {
                    // 和上面的V1一样，不管了
                }
                else if (handlePK(json))
                {
                }
                else if (cmd == "HOT_RANK_CHANGED_V2")
                {
                    /*{
                        "cmd": "HOT_RANK_CHANGED_V2",
                        "data": {
                            "rank": 0,
                            "trend": 0,
                            "countdown": 1070,
                            "timestamp": 1652929930,
                            "web_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=2\\u0026area_id=1\\u0026parent_area_id=1\\u0026second_area_id=145",
                            "live_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=1\\u0026area_id=1\\u0026parent_area_id=1\\u0026second_area_id=145\\u0026is_live_half_webview=1\\u0026hybrid_rotate_d=1\\u0026hybrid_half_ui=1,3,100p,70p,ffffff,0,30,100,12,0;2,2,375,100p,ffffff,0,30,100,0,0;3,3,100p,70p,ffffff,0,30,100,12,0;4,2,375,100p,ffffff,0,30,100,0,0;5,3,100p,70p,ffffff,0,30,100,0,0;6,3,100p,70p,ffffff,0,30,100,0,0;7,3,100p,70p,ffffff,0,30,100,0,0;8,3,100p,70p,ffffff,0,30,100,0,0",
                            "blink_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=3\\u0026area_id=1\\u0026parent_area_id=1\\u0026second_area_id=145\\u0026is_live_half_webview=1\\u0026hybrid_rotate_d=1\\u0026is_cling_player=1\\u0026hybrid_half_ui=1,3,100p,70p,ffffff,0,30,100,0,0;2,2,375,100p,ffffff,0,30,100,0,0;3,3,100p,70p,ffffff,0,30,100,0,0;4,2,375,100p,ffffff,0,30,100,0,0;5,3,100p,70p,ffffff,0,30,100,0,0;6,3,100p,70p,ffffff,0,30,100,0,0;7,3,100p,70p,ffffff,0,30,100,0,0;8,3,100p,70p,ffffff,0,30,100,0,0",
                            "live_link_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=5\\u0026area_id=1\\u0026parent_area_id=1\\u0026second_area_id=145\\u0026is_live_half_webview=1\\u0026hybrid_rotate_d=1\\u0026is_cling_player=1\\u0026hybrid_half_ui=1,3,100p,70p,f4eefa,0,30,100,0,0;2,2,375,100p,f4eefa,0,30,100,0,0;3,3,100p,70p,f4eefa,0,30,100,0,0;4,2,375,100p,f4eefa,0,30,100,0,0;5,3,100p,70p,f4eefa,0,30,100,0,0;6,3,100p,70p,f4eefa,0,30,100,0,0;7,3,100p,70p,f4eefa,0,30,100,0,0;8,3,100p,70p,f4eefa,0,30,100,0,0",
                            "pc_link_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=4\\u0026is_live_half_webview=1\\u0026area_id=1\\u0026parent_area_id=1\\u0026second_area_id=145\\u0026pc_ui=338,465,f4eefa,0",
                            "icon": "https://i0.hdslb.com/bfs/live/cb2e160ac4f562b347bb5ae6e635688ebc69580f.png",
                            "area_name": "视频聊天",
                            "rank_desc": ""
                        }
                    }*/
                    QJsonObject data = json.value("data").toObject();
                    int countdown = data.value("countdown").toInt();
                }
                else if (cmd == "HOT_ROOM_NOTIFY")
                {
                }
                else
                {
                    qWarning() << "未处理的命令=" << cmd << "   正文=" << QString(body);
                }

                triggerCmdEvent(cmd, LiveDanmaku(json.value("data").toObject()));
            }
            else
            {
                qWarning() << s8("未知协议：") << protover << s8("，若有必要请处理");
                qWarning() << s8("未知正文：") << body;
            }
        }
    }
    else
    {
        qWarning() << "未处理的包类型：operation =" << operation;
    }
//    delete[] body.data();
//    delete[] message.data();
    SOCKET_DEB << "消息处理结束";
}

/**
 * 博客来源：https://blog.csdn.net/doujianyoutiao/article/details/106236207
 */
QByteArray zlibToQtUncompr(const char *pZLIBData, uLongf dataLen/*, uLongf srcDataLen = 0x100000*/)
{
    char *pQtData = new char[dataLen + 4];
    char *pByte = (char *)(&dataLen);/*(char *)(&srcDataLen);*/
    pQtData[3] = *pByte;
    pQtData[2] = *(pByte + 1);
    pQtData[1] = *(pByte + 2);
    pQtData[0] = *(pByte + 3);
    memcpy(pQtData + 4, pZLIBData, dataLen);
    QByteArray qByteArray(pQtData, dataLen + 4);
    delete[] pQtData;
    return qUncompress(qByteArray);
}

void MainWindow::slotUncompressBytes(const QByteArray &body)
{
    splitUncompressedBody(zlibToQtUncompr(body.data(), body.size()+1));
}

void MainWindow::splitUncompressedBody(const QByteArray &unc)
{
    int offset = 0;
    short headerSize = 16;
    while (offset < unc.size() - headerSize)
    {
        int packSize = ((uchar)unc[offset+0] << 24)
                + ((uchar)unc[offset+1] << 16)
                + ((uchar)unc[offset+2] << 8)
                + (uchar)unc[offset+3];
        QByteArray jsonBa = unc.mid(offset + headerSize, packSize - headerSize);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(jsonBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << s8("解析解压后的JSON出错：") << error.errorString();
            qCritical() << s8("包数值：") << offset << packSize << "  解压后大小：" << unc.size();
            qCritical() << s8(">>当前JSON") << jsonBa;
            qCritical() << s8(">>解压正文") << unc;
            return ;
        }
        QJsonObject json = document.object();
        QString cmd = json.value("cmd").toString();
        SOCKET_INF << "解压后获取到CMD：" << cmd;
        if (saveToSqlite && saveCmdToSqlite)
        {
            sqlService.insertCmd(cmd, jsonBa);
        }
        if (cmd != "ROOM_BANNER" && cmd != "ACTIVITY_BANNER_UPDATE_V2" && cmd != "PANEL"
                && cmd != "ONLINERANK")
            SOCKET_INF << "单个JSON消息：" << offset << packSize << QString(jsonBa);
        try {
            handleMessage(json);
        } catch (...) {
            qCritical() << s8("出错啦") << jsonBa;
        }

        offset += packSize;
    }
}

/**
 * 数据包解析： https://segmentfault.com/a/1190000017328813?utm_source=tag-newest#tagDataPackage
 */
void MainWindow::handleMessage(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    qInfo() << s8(">消息命令ZCOM：") << cmd;
    if (cmd == "LIVE") // 开播？
    {
        if (ui->recordCheck->isChecked())
            startLiveRecord();
        reconnectWSDuration = INTERVAL_RECONNECT_WS;
        emit signalLiveStart(ac->roomId);

        if (isLiving() || pking || pkToLive + 30 > QDateTime::currentSecsSinceEpoch()) // PK导致的开播下播情况
        {
            qInfo() << "忽视PK导致的开播情况";
            // 大乱斗时突然断联后恢复
            if (!isLiving())
            {
                if (ui->timerConnectServerCheck->isChecked() && connectServerTimer->isActive())
                    connectServerTimer->stop();
                slotStartWork();
            }
            return ;
        }
        QString roomId = json.value("roomid").toString();
//        if (roomId == this->roomId || roomId == this->shortId) // 是当前房间的
        {
            QString text = ui->startLiveWordsEdit->text();
            if (ui->startLiveSendCheck->isChecked() && !text.trimmed().isEmpty()
                    && QDateTime::currentMSecsSinceEpoch() - liveTimestamp > 60000) // 起码是上次下播10秒钟后
                sendAutoMsg(text, LiveDanmaku());
            ui->liveStatusButton->setText("已开播");
            ac->liveStatus = 1;
            if (ui->timerConnectServerCheck->isChecked() && connectServerTimer->isActive())
                connectServerTimer->stop();
            slotStartWork(); // 每个房间第一次开始工作
        }
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "PREPARING") // 下播
    {
        finishLiveRecord();
        reconnectWSDuration = INTERVAL_RECONNECT_WS;

        if (pking || pkToLive + 30 > QDateTime::currentSecsSinceEpoch()) // PK导致的开播下播情况
            return ;
        QString roomId = json.value("roomid").toString();
//        if (roomId == this->roomId || roomId == this->shortId) // 是当前房间的
        {
            QString text = ui->endLiveWordsEdit->text();
            if (ui->startLiveSendCheck->isChecked() &&!text.trimmed().isEmpty()
                    && QDateTime::currentMSecsSinceEpoch() - liveTimestamp > 600000) // 起码是十分钟后再播报，万一只是尝试开播呢
                sendAutoMsg(text, LiveDanmaku());
            ui->liveStatusButton->setText("已下播");
            ac->liveStatus = 0;

            if (ui->timerConnectServerCheck->isChecked() && !connectServerTimer->isActive())
                connectServerTimer->start();
        }

        releaseLiveData(true);
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "ROOM_CHANGE")
    {
        getRoomInfo(false);
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "DANMU_MSG" || cmd.startsWith("DANMU_MSG:")) // 收到弹幕
    {
        QJsonArray info = json.value("info").toArray();
        if (info.size() <= 2)
            QMessageBox::information(this, "弹幕数据 info", QString(QJsonDocument(info).toJson()));
        QJsonArray array = info[0].toArray();
        if (array.size() <= 3)
            QMessageBox::information(this, "弹幕数据 array", QString(QJsonDocument(array).toJson()));
        qint64 textColor = array[3].toInt(); // 弹幕颜色
        qint64 timestamp = static_cast<qint64>(array[4].toDouble());
        QString msg = info[1].toString();
        QJsonArray user = info[2].toArray();
        if (user.size() <= 1)
            QMessageBox::information(this, "弹幕数据 user", QString(QJsonDocument(user).toJson()));
        qint64 uid = static_cast<qint64>(user[0].toDouble());
        QString username = user[1].toString();
        int admin = user[2].toInt(); // 是否为房管（实测现在主播不属于房管了）
        int vip = user[3].toInt(); // 是否为老爷
        int svip = user[4].toInt(); // 是否为年费老爷
        int uidentity = user[5].toInt(); // 是否为非正式会员或正式会员（5000非，10000正）
        int iphone = user[6].toInt(); // 是否绑定手机
        QString unameColor = user[7].toString();
        int level = info[4].toArray()[0].toInt();
        QJsonArray medal = info[3].toArray();
        int uguard = info[7].toInt(); // 用户本房间舰队身份：0非，1总督，2提督，3舰长
        int medal_level = 0;
        if (array.size() >= 15) // info[0][14]: voice object
        {
            MyJson voice = array.at(14).toObject();
            JS(voice, voice_url); // 下载直链
            JS(voice, text); // 语音文字
            JI(voice, file_duration); // 秒数

            // 唔，好吧，啥都不用做
        }

        // 判断对面直播间
        bool opposite = pking &&
                ((oppositeAudience.contains(uid) && !myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() && medal.size() >= 4 &&
                     snum(static_cast<qint64>(medal[3].toDouble())) == pkRoomId));

        // !弹幕的时间戳是13位，其他的是10位！
        qInfo() << s8("接收到弹幕：") << username << msg << QDateTime::fromMSecsSinceEpoch(timestamp);
        /*QString localName = danmakuWindow->getLocalNickname(uid);
        if (!localName.isEmpty())
            username = localName;*/

        // 统计弹幕次数
        int danmuCount = us->danmakuCounts->value("danmaku/"+snum(uid), 0).toInt()+1;
        us->danmakuCounts->setValue("danmaku/"+snum(uid), danmuCount);
        dailyDanmaku++;
        if (dailySettings)
            dailySettings->setValue("danmaku", dailyDanmaku);

        // 等待通道
        if (uid != ac->cookieUid.toLongLong())
        {
            for (int i = 0; i < CHANNEL_COUNT; i++)
                msgWaits[i]++;
        }

        // 添加到列表
        QString cs = QString::number(textColor, 16);
        while (cs.size() < 6)
            cs = "0" + cs;
        LiveDanmaku danmaku(username, msg, uid, level, QDateTime::fromMSecsSinceEpoch(timestamp),
                                                 unameColor, "#"+cs);
        danmaku.setUserInfo(admin, vip, svip, uidentity, iphone, uguard);
        if (medal.size() >= 4)
        {
            medal_level = medal[0].toInt();
            danmaku.setMedal(snum(static_cast<qint64>(medal[3].toDouble())),
                    medal[1].toString(), medal_level, medal[2].toString());
        }
        if (snum(uid) == ac->cookieUid && noReplyMsgs.contains(msg))
        {
            danmaku.setNoReply();
            danmaku.setAutoSend();
            noReplyMsgs.removeOne(msg);
        }
        else
            minuteDanmuPopul++;
        danmaku.setOpposite(opposite);
        appendNewLiveDanmaku(danmaku);

        // 进入累计
        ui->danmuCountLabel->setText(snum(++liveTotalDanmaku));

        // 新人发言
        if (danmuCount == 1)
        {
            dailyNewbieMsg++;
            if (dailySettings)
                dailySettings->setValue("newbie_msg", dailyNewbieMsg);
        }

        // 新人小号禁言
        bool blocked = false;
        auto testTipBlock = [&]{
            if (danmakuWindow && !ui->promptBlockNewbieKeysEdit->toPlainText().trimmed().isEmpty())
            {
                QString reStr = ui->promptBlockNewbieKeysEdit->toPlainText();
                if (reStr.endsWith("|"))
                    reStr = reStr.left(reStr.length()-1);
                if (msg.indexOf(QRegularExpression(reStr)) > -1) // 提示拉黑
                {
                    blocked = true;
                    danmakuWindow->showFastBlock(uid, msg);
                }
            }
        };
        if (!debugPrint && (snum(uid) == ac->upUid || snum(uid) == ac->cookieUid)) // 是自己或UP主的，不屏蔽
        {
            // 不仅不屏蔽，反而支持主播特权
            processRemoteCmd(msg);
        }
        else if (admin && ui->allowAdminControlCheck->isChecked()) // 房管特权
        {
            // 开放给房管的特权
            processRemoteCmd(msg);
        }
        else if (ui->blockNotOnlyNewbieCheck->isChecked() || (level == 0 && medal_level <= 1 && danmuCount <= 3) || danmuCount <= 1)
        {
            // 尝试自动拉黑
            if (ui->autoBlockNewbieCheck->isChecked() && !ui->autoBlockNewbieKeysEdit->toPlainText().trimmed().isEmpty())
            {
                QString reStr = ui->autoBlockNewbieKeysEdit->toPlainText();
                if (reStr.endsWith("|"))
                    reStr = reStr.left(reStr.length()-1);
                translateUnicode(reStr);
                QRegularExpression re(reStr);
                if (!re.isValid())
                    showError("错误的禁言关键词表达式");
                QRegularExpressionMatch match;
                if (msg.indexOf(re, 0, &match) > -1 // 自动拉黑
                        && (ui->blockNotOnlyNewbieCheck->isChecked()
                            || (danmaku.getAnchorRoomid() != ac->roomId // 不带有本房间粉丝牌
                                && !isInFans(uid) // 未刚关注主播（新人一般都是刚关注吧，在第一页）
                                && medal_level <= 2))) // 勋章不到3级
                {
                    if (match.capturedTexts().size() >= 1)
                    {
                        QString blockKey = match.capturedTexts().size() >= 2 ? match.captured(1) : match.captured(0); // 第一个括号的
                        localNotify("自动禁言【" + blockKey + "】");
                    }
                    qInfo() << "检测到新人违禁词，自动拉黑：" << username << msg;

                    if (!isFilterRejected("FILTER_KEYWORD_BLOCK", danmaku)) // 阻止自动禁言过滤器
                    {
                        // 拉黑
                        addBlockUser(uid, ui->autoBlockTimeSpin->value());
                        blocked = true;

                        // 通知
                        if (ui->autoBlockNewbieNotifyCheck->isChecked())
                        {
                            static int prevNotifyInCount = -20; // 上次发送通知时的弹幕数量
                            if (rt->allDanmakus.size() - prevNotifyInCount >= 20) // 最低每20条发一遍
                            {
                                prevNotifyInCount = rt->allDanmakus.size();

                                QStringList words = getEditConditionStringList(ui->autoBlockNewbieNotifyWordsEdit->toPlainText(), danmaku);
                                if (words.size())
                                {
                                    int r = qrand() % words.size();
                                    QString s = words.at(r);
                                    if (!s.trimmed().isEmpty())
                                    {
                                        sendNotifyMsg(s, danmaku);
                                    }
                                }
                                else if (debugPrint)
                                {
                                    localNotify("[没有可发送的禁言通知弹幕]");
                                }
                            }
                        }
                    }
                }
            }

            // 没有被禁言，那么判断提示拉黑
            if (!blocked && ui->promptBlockNewbieCheck->isChecked())
            {
                testTipBlock();
            }
        }
        else if (ui->promptBlockNewbieCheck->isChecked() && ui->notOnlyNewbieCheck->isChecked())
        {
            // 判断提示拉黑
            testTipBlock();
        }

        if (!blocked)
            markNotRobot(uid);

        triggerCmdEvent("DANMU_MSG", danmaku.with(json));
    }
    else if (cmd == "SEND_GIFT") // 有人送礼
    {
        /*{
            "cmd": "SEND_GIFT",
            "data": {
                "draw": 0,
                "gold": 0,
                "silver": 0,
                "num": 1,
                "total_coin": 0,
                "effect": 0,
                "broadcast_id": 0,
                "crit_prob": 0,
                "guard_level": 0,
                "rcost": 200773,
                "uid": 20285041,
                "timestamp": 1614439816,
                "giftId": 30607,
                "giftType": 5,
                "super": 0,
                "super_gift_num": 1,
                "super_batch_gift_num": 1,
                "remain": 19,
                "price": 0,
                "beatId": "",
                "biz_source": "Live",
                "action": "投喂",
                "coin_type": "silver",
                "uname": "懒一夕智能科技",
                "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
                "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30607:1614439816.1655",      // 多个以及最后的 COMBO_SEND 是一样的
                "rnd": "34158224",
                "giftName": "小心心",
                "combo_send": null,
                "batch_combo_send": null,
                "tag_image": "",
                "top_list": null,
                "send_master": null,
                "is_first": true,                   // 不是第一个就是false
                "demarcation": 1,
                "combo_stay_time": 3,
                "combo_total_coin": 1,
                "tid": "1614439816120100003",
                "effect_block": 1,
                "is_special_batch": 0,
                "combo_resources_id": 1,
                "magnification": 1,
                "name_color": "",
                "medal_info": {
                    "target_id": 0,
                    "special": "",
                    "icon_id": 0,
                    "anchor_uname": "",
                    "anchor_roomid": 0,
                    "medal_level": 0,
                    "medal_name": "",
                    "medal_color": 0,
                    "medal_color_start": 0,
                    "medal_color_end": 0,
                    "medal_color_border": 0,
                    "is_lighted": 0,
                    "guard_level": 0
                },
                "svga_block": 0
            }
        }*/
        /*{
            "cmd": "SEND_GIFT",
            "data": {
                "draw": 0,
                "gold": 0,
                "silver": 0,
                "num": 1,
                "total_coin": 1000,
                "effect": 0,                // 太便宜的礼物没有
                "broadcast_id": 0,
                "crit_prob": 0,
                "guard_level": 0,
                "rcost": 200795,
                "uid": 20285041,
                "timestamp": 1614440753,
                "giftId": 30823,
                "giftType": 0,
                "super": 0,
                "super_gift_num": 1,
                "super_batch_gift_num": 1,
                "remain": 0,
                "price": 1000,
                "beatId": "",
                "biz_source": "Live",
                "action": "投喂",
                "coin_type": "gold",
                "uname": "懒一夕智能科技",
                "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
                "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30823:1614440753.1786",
                "rnd": "245758485",
                "giftName": "小巧花灯",
                "combo_send": {
                    "uid": 20285041,
                    "gift_num": 1,
                    "combo_num": 1,
                    "gift_id": 30823,
                    "combo_id": "gift:combo_id:20285041:2070473390:30823:1614440753.1781",
                    "gift_name": "小巧花灯",
                    "action": "投喂",
                    "uname": "懒一夕智能科技",
                    "send_master": null
                },
                "batch_combo_send": {
                    "uid": 20285041,
                    "gift_num": 1,
                    "batch_combo_num": 1,
                    "gift_id": 30823,
                    "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30823:1614440753.1786",
                    "gift_name": "小巧花灯",
                    "action": "投喂",
                    "uname": "懒一夕智能科技",
                    "send_master": null
                },
                "tag_image": "",
                "top_list": null,
                "send_master": null,
                "is_first": true,
                "demarcation": 2,
                "combo_stay_time": 3,
                "combo_total_coin": 1000,
                "tid": "1614440753121100001",
                "effect_block": 0,
                "is_special_batch": 0,
                "combo_resources_id": 1,
                "magnification": 1,
                "name_color": "",
                "medal_info": {
                    "target_id": 0,
                    "special": "",
                    "icon_id": 0,
                    "anchor_uname": "",
                    "anchor_roomid": 0,
                    "medal_level": 0,
                    "medal_name": "",
                    "medal_color": 0,
                    "medal_color_start": 0,
                    "medal_color_end": 0,
                    "medal_color_border": 0,
                    "is_lighted": 0,
                    "guard_level": 0
                },
                "svga_block": 0
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        int giftId = data.value("giftId").toInt();
        int giftType = data.value("giftType").toInt(); // 不知道是啥，金瓜子1，银瓜子（小心心、辣条）5？
        QString giftName = data.value("giftName").toString();
        QString username = data.value("uname").toString();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        int num = data.value("num").toInt();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble()); // 秒
        timestamp = QDateTime::currentSecsSinceEpoch(); // *不管送出礼物的时间，只管机器人接收到的时间
        QString coinType = data.value("coin_type").toString();
        int totalCoin = data.value("total_coin").toInt();

        qInfo() << s8("接收到送礼：") << username << giftId << giftName << num << s8("  总价值：") << totalCoin << coinType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        if (!data.value("medal_info").isNull())
        {
            QJsonObject medalInfo = data.value("medal_info").toObject();
            QString anchorRoomId = snum(qint64(medalInfo.value("anchor_room_id").toDouble())); // !注意：这个一直为0！
            QString anchorUname = medalInfo.value("anchor_uname").toString(); // !注意：也是空的
            int guardLevel = medalInfo.value("guard_level").toInt();
            int isLighted = medalInfo.value("is_lighted").toInt();
            int medalColor = medalInfo.value("medal_color").toInt();
            int medalColorBorder = medalInfo.value("medal_color_border").toInt();
            int medalColorEnd = medalInfo.value("medal_color_end").toInt();
            int medalColorStart = medalInfo.value("medal_color_start").toInt();
            int medalLevel = medalInfo.value("medal_level").toInt();
            QString medalName = medalInfo.value("medal_name").toString();
            QString spacial = medalInfo.value("special").toString();
            QString targetId = snum(qint64(medalInfo.value("target_id").toDouble())); // 目标用户ID
            if (!medalName.isEmpty())
            {
                QString cs = QString::number(medalColor, 16);
                while (cs.size() < 6)
                    cs = "0" + cs;
                danmaku.setMedal(anchorRoomId, medalName, medalLevel, cs, anchorUname);
            }
        }

        bool merged = mergeGiftCombo(danmaku); // 如果有合并，则合并到之前的弹幕上面
        danmaku.setFirst(merged ? 0 : 1);
        if (!merged)
        {
            appendNewLiveDanmaku(danmaku);
            addGuiGiftList(danmaku);
        }
        if (ui->saveEveryGiftCheck->isChecked())
            saveEveryGift(danmaku);

        if (!justStart && ui->autoSendGiftCheck->isChecked()) // 是否需要礼物答谢
        {
            QJsonValue batchComboIdVal = data.value("batch_combo_id");
            QString batchComboId = batchComboIdVal.toString();
            if (!ui->giftComboSendCheck->isChecked() || batchComboIdVal.isNull()) // 立刻发送
            {
                // 如果合并了，那么可能已经感谢了，就不用管了
                if (!merged)
                {
                    QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
                    if (words.size())
                    {
                        int r = qrand() % words.size();
                        QString msg = words.at(r);
                        if (us->strongNotifyUsers.contains(uid))
                        {
                            if (debugPrint)
                                localNotify("[强提醒]");
                            sendCdMsg(msg, danmaku, NOTIFY_CD, GIFT_CD_CN,
                                      ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked(), false);
                        }
                        else
                            sendGiftMsg(msg, danmaku);
                    }
                    else if (debugPrint)
                    {
                        localNotify("[没有可发送的礼物答谢弹幕]");
                    }
                }
                else if (debugPrint)
                {
                    localNotify("[礼物被合并，不答谢]");
                }
            }
            else // 延迟发送
            {
                if (giftCombos.contains(batchComboId)) // 已经连击了，合并
                {
                    giftCombos[batchComboId].addGift(num, totalCoin, QDateTime::currentDateTime());
                }
                else // 创建新的连击
                {
                    danmaku.setTime(QDateTime::currentDateTime());
                    giftCombos.insert(batchComboId, danmaku);
                    if (!comboTimer->isActive())
                        comboTimer->start();
                }
            }
        }

        if (coinType == "silver")
        {
            qint64 userSilver = us->danmakuCounts->value("silver/" + snum(uid)).toLongLong();
            userSilver += totalCoin;
            us->danmakuCounts->setValue("silver/"+snum(uid), userSilver);

            dailyGiftSilver += totalCoin;
            if (dailySettings)
                dailySettings->setValue("gift_silver", dailyGiftSilver);
        }
        if (coinType == "gold")
        {
            qint64 userGold = us->danmakuCounts->value("gold/" + snum(uid)).toLongLong();
            userGold += totalCoin;
            us->danmakuCounts->setValue("gold/"+snum(uid), userGold);

            dailyGiftGold += totalCoin;
            if (dailySettings)
                dailySettings->setValue("gift_gold", dailyGiftGold);

            // 正在PK，保存弹幕历史
            // 因为最后的大乱斗最佳助攻只提供名字，所以这里需要保存 uname->uid 的映射
            // 方便起见，直接全部保存下来了
            pkGifts.append(danmaku);

            // 添加礼物记录
            appendLiveGift(danmaku);

            // 正在偷塔阶段
            if (pkEnding && uid == ac->cookieUid.toLongLong()) // 机器人账号
            {
//                pkVoting -= totalCoin;
//                if (pkVoting < 0) // 自己用其他设备送了更大的礼物
//                {
//                    pkVoting = 0;
//                }
            }
        }

        // 都送礼了，总该不是机器人了吧
        markNotRobot(uid);

        // 监听勋章升级
        if (ui->listenMedalUpgradeCheck->isChecked())
        {
            detectMedalUpgrade(danmaku);
        }

        triggerCmdEvent(cmd, danmaku.with(data));
    }
    else if (cmd == "COMBO_SEND") // 连击礼物
    {
        /*{
            "cmd": "COMBO_SEND",
            "data": {
                "action": "投喂",
                "batch_combo_id": "batch:gift:combo_id:8833188:354580019:30607:1610168283.0188",
                "batch_combo_num": 9,
                "combo_id": "gift:combo_id:8833188:354580019:30607:1610168283.0182",
                "combo_num": 9,
                "combo_total_coin": 0,
                "gift_id": 30607,
                "gift_name": "小心心",
                "gift_num": 0,
                "is_show": 1,
                "medal_info": {
                    "anchor_roomid": 0,
                    "anchor_uname": "",
                    "guard_level": 0,
                    "icon_id": 0,
                    "is_lighted": 1,
                    "medal_color": 1725515,
                    "medal_color_border": 1725515,
                    "medal_color_end": 5414290,
                    "medal_color_start": 1725515,
                    "medal_level": 23,
                    "medal_name": "好听",
                    "special": "",
                    "target_id": 354580019
                },
                "name_color": "",
                "r_uname": "薄薄的温酱",
                "ruid": 354580019,
                "send_master": null,
                "total_num": 9,
                "uid": 8833188,
                "uname": "南酱的可露儿"
            }
        }*/
        /*{
            "cmd": "COMBO_SEND",
            "data": {
                "uid": 20285041,
                "ruid": 2070473390,
                "uname": "懒一夕智能科技",
                "r_uname": "喵大王_cat",
                "combo_num": 4,
                "gift_id": 30607,
                "gift_num": 0,
                "batch_combo_num": 4,
                "gift_name": "小心心",
                "action": "投喂",
                "combo_id": "gift:combo_id:20285041:2070473390:30607:1614439816.1648",
                "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30607:1614439816.1655",
                "is_show": 1,
                "send_master": null,
                "name_color": "",
                "total_num": 4,
                "medal_info": {
                    "target_id": 0,
                    "special": "",
                    "icon_id": 0,
                    "anchor_uname": "",
                    "anchor_roomid": 0,
                    "medal_level": 0,
                    "medal_name": "",
                    "medal_color": 0,
                    "medal_color_start": 0,
                    "medal_color_end": 0,
                    "medal_color_border": 0,
                    "is_lighted": 0,
                    "guard_level": 0
                },
                "combo_total_coin": 0
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "SUPER_CHAT_MESSAGE") // 醒目留言
    {
        /*{
            "cmd": "SUPER_CHAT_MESSAGE",
            "data": {
                "background_bottom_color": "#2A60B2",
                "background_color": "#EDF5FF",
                "background_color_end": "#405D85",
                "background_color_start": "#3171D2",
                "background_icon": "",
                "background_image": "https://i0.hdslb.com/bfs/live/a712efa5c6ebc67bafbe8352d3e74b820a00c13e.png",
                "background_price_color": "#7497CD",
                "color_point": 0.7,
                "end_time": 1613125905,
                "gift": {
                    "gift_id": 12000,
                    "gift_name": "醒目留言", // 这个是特殊礼物？
                    "num": 1
                },
                "id": 1278390,
                "is_ranked": 0,
                "is_send_audit": "0",
                "medal_info": {
                    "anchor_roomid": 1010,
                    "anchor_uname": "KB呆又呆",
                    "guard_level": 3,
                    "icon_id": 0,
                    "is_lighted": 1,
                    "medal_color": "#6154c",
                    "medal_color_border": 6809855,
                    "medal_color_end": 6850801,
                    "medal_color_start": 398668,
                    "medal_level": 25,
                    "medal_name": "KKZ",
                    "special": "",
                    "target_id": 389088
                },
                "message": "最右边可以爬上去",
                "message_font_color": "#A3F6FF",
                "message_trans": "",
                "price": 30,
                "rate": 1000,
                "start_time": 1613125845,
                "time": 60,
                "token": "767AB474",
                "trans_mark": 0,
                "ts": 1613125845,
                "uid": 35030958,
                "user_info": {
                    "face": "http://i0.hdslb.com/bfs/face/cdd8fbb13b2034dc3651096cbeef4b5e89765c35.jpg",
                    "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                    "guard_level": 3,
                    "is_main_vip": 1,
                    "is_svip": 0,
                    "is_vip": 0,
                    "level_color": "#61c05a",
                    "manager": 0,
                    "name_color": "#00D1F1",
                    "title": "0",
                    "uname": "么么么么么么么么句号",
                    "user_level": 14
                }
            },
            "roomid": "1010"
        }*/

        MyJson data = json.value("data").toObject();
        JL(data, uid);
        JS(data, message);
        JS(data, message_font_color);
        JL(data, end_time); // 秒
        JI(data, price); // 注意这个是价格（单位元），不是金瓜子，并且30起步

        JO(data, gift);
        JI(gift, gift_id);
        JS(gift, gift_name);
        JI(gift, num);

        JO(data, user_info);
        JS(user_info, uname);
        JI(user_info, user_level);
        JS(user_info, name_color);
        JI(user_info, is_main_vip);
        JI(user_info, is_vip);
        JI(user_info, is_svip);

        JO(data, medal_info);
        JL(medal_info, anchor_roomid);
        JS(medal_info, anchor_uname);
        JI(medal_info, guard_level);
        JI(medal_info, medal_level);
        JS(medal_info, medal_color);
        JS(medal_info, medal_name);
        JL(medal_info, target_id);

        if (gift_id != 12000) // 醒目留言
        {
            qWarning() << "非醒目留言的特殊聊天消息：" << json;
            return ;
        }

        LiveDanmaku danmaku(uname, message, uid, user_level, QDateTime::fromSecsSinceEpoch(end_time), name_color, message_font_color,
                    gift_id, gift_name, num, price);
        danmaku.setMedal(snum(anchor_roomid), medal_name, medal_level, medal_color, anchor_uname);
        appendNewLiveDanmaku(danmaku);

        pkGifts.append(danmaku);
        triggerCmdEvent(cmd, danmaku.with(data));
    }
    else if (cmd == "SUPER_CHAT_MESSAGE_JPN") // 醒目留言日文翻译
    {
        /*{
            "cmd": "SUPER_CHAT_MESSAGE_JPN",
            "data": {
                "background_bottom_color": "#2A60B2",
                "background_color": "#EDF5FF",
                "background_icon": "",
                "background_image": "https://i0.hdslb.com/bfs/live/a712efa5c6ebc67bafbe8352d3e74b820a00c13e.png",
                "background_price_color": "#7497CD",
                "end_time": 1613125905,
                "gift": {
                    "gift_id": 12000,
                    "gift_name": "醒目留言",
                    "num": 1
                },
                "id": "1278390",
                "is_ranked": 0,
                "medal_info": {
                    "anchor_roomid": 1010,
                    "anchor_uname": "KB呆又呆",
                    "icon_id": 0,
                    "medal_color": "#6154c",
                    "medal_level": 25,
                    "medal_name": "KKZ",
                    "special": "",
                    "target_id": 389088
                },
                "message": "最右边可以爬上去",
                "message_jpn": "",
                "price": 30,
                "rate": 1000,
                "start_time": 1613125845,
                "time": 60,
                "token": "767AB474",
                "ts": 1613125845,
                "uid": "35030958",
                "user_info": {
                    "face": "http://i0.hdslb.com/bfs/face/cdd8fbb13b2034dc3651096cbeef4b5e89765c35.jpg",
                    "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                    "guard_level": 3,
                    "is_main_vip": 1,
                    "is_svip": 0,
                    "is_vip": 0,
                    "level_color": "#61c05a",
                    "manager": 0,
                    "title": "0",
                    "uname": "么么么么么么么么句号",
                    "user_level": 14
                }
            },
            "roomid": "1010"
        }*/
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "SUPER_CHAT_MESSAGE_DELETE") // 删除醒目留言
    {
        qInfo() << "删除醒目留言：" << json;
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "SPECIAL_GIFT") // 节奏风暴（特殊礼物？）
    {
        /*{
            "cmd": "SPECIAL_GIFT",
            "data": {
                "39": {
                    "action": "start",
                    "content": "糟了，是心动的感觉！",
                    "hadJoin": 0,
                    "id": "3072200788973",
                    "num": 1,
                    "storm_gif": "http://static.hdslb.com/live-static/live-room/images/gift-section/mobilegift/2/jiezou.gif?2017011901",
                    "time": 90
                }
            }
        }*/

        QJsonObject data = json.value("data").toObject();
        QJsonObject sg = data.value("39").toObject();
        QString text = sg.value("content").toString();
        qint64 id = qint64(sg.value("id").toDouble());
        int time = sg.value("time").toInt();
        if (danmakuWindow)
        {
            danmakuWindow->addBlockText(text);
            QTimer::singleShot(time * 1000, danmakuWindow, [=]{
                danmakuWindow->removeBlockText(text);
            });
        }
        qInfo() << "节奏风暴：" << text << ui->autoLOTCheck->isChecked();
        if (ui->autoLOTCheck->isChecked())
        {
            joinStorm(id);
        }

        triggerCmdEvent(cmd, LiveDanmaku().with(data));
    }
    else if (cmd == "WELCOME_GUARD") // 舰长进入（不会触发），通过guard_level=1/2/3分辨总督/提督/舰长
    {
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("username").toString();
        qint64 startTime = static_cast<qint64>(data.value("start_time").toDouble());
        qint64 endTime = static_cast<qint64>(data.value("end_time").toDouble());
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        QString unameColor = data.value("uname_color").toString();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        qInfo() << s8("舰长进入：") << username;
        /*QString localName = danmakuWindow->getLocalNickname(uid);
        if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(LiveDanmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp)
                                        , true, unameColor, spreadDesc, spreadInfo));
        appendNewLiveDanmaku(danmaku);

        triggerCmdEvent(cmd, danmaku.with(data));
    }
    else  if (cmd == "ENTRY_EFFECT") // 舰长进入、高能榜（不知道到榜几）、姥爷的同时会出现
    {
        // 欢迎舰长
        /*{
            "cmd": "ENTRY_EFFECT",
            "data": {
                "id": 4,
                "uid": 20285041,
                "target_id": 688893202,
                "mock_effect": 0,
                "face": "https://i2.hdslb.com/bfs/face/24420cdcb6eeb119dbcd1f1843fdd8ada5b7d045.jpg",
                "privilege_type": 3,
                "copy_writing": "欢迎舰长 \\u003c%心乂鸽鸽%\\u003e 进入直播间",
                "copy_color": "#ffffff",
                "highlight_color": "#E6FF00",
                "priority": 70,
                "basemap_url": "https://i0.hdslb.com/bfs/live/mlive/f34c7441cdbad86f76edebf74e60b59d2958f6ad.png",
                "show_avatar": 1,
                "effective_time": 2,
                "web_basemap_url": "",
                "web_effective_time": 0,
                "web_effect_close": 0,
                "web_close_time": 0,
                "business": 1,
                "copy_writing_v2": "欢迎 \\u003c^icon^\\u003e 舰长 \\u003c%心乂鸽鸽%\\u003e 进入直播间",
                "icon_list": [
                    2
                ],
                "max_delay_time": 7
            }
        }*/

        // 欢迎姥爷
        /*{
            "cmd": "ENTRY_EFFECT",
            "data": {
                "basemap_url": "https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png",
                "business": 3,
                "copy_color": "#000000",
                "copy_writing": "欢迎 <%___君陌%> 进入直播间",
                "copy_writing_v2": "欢迎 <^icon^> <%___君陌%> 进入直播间",
                "effective_time": 2,
                "face": "https://i1.hdslb.com/bfs/face/8fb8336e1ae50001ca76b80c30b01d23b07203c9.jpg",
                "highlight_color": "#FFF100",
                "icon_list": [
                    2
                ],
                "id": 136,
                "max_delay_time": 7,
                "mock_effect": 0,
                "priority": 1,
                "privilege_type": 0,
                "show_avatar": 1,
                "target_id": 5988102,
                "uid": 453364,
                "web_basemap_url": "https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png",
                "web_close_time": 900,
                "web_effect_close": 0,
                "web_effective_time": 2
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString copy_writing = data.value("copy_writing").toString();
        QStringList results = QRegularExpression("欢迎(舰长|提督|总督)?.+?<%(.+)%>").match(copy_writing).capturedTexts();
        LiveDanmaku danmaku;
        if (results.size() < 2 || results.at(1).isEmpty()) // 不是船员
        {
            qInfo() << "高能榜进入：" << copy_writing;
            QStringList results = QRegularExpression("^欢迎(尊享用户)?\\s*<%(.+)%>").match(copy_writing).capturedTexts();
            if (results.size() < 2)
            {
                qWarning() << "识别舰长进入失败：" << copy_writing;
                qWarning() << data;
                return ;
            }

            // 高能榜上的用户
            for (int i = 0; i < onlineGoldRank.size(); i++)
            {
                if (onlineGoldRank.at(i).getUid() == uid)
                {
                    danmaku = onlineGoldRank.at(i); // 就是这个用户了
                    danmaku.setTime(QDateTime::currentDateTime());
                    qInfo() << "                guard:" << danmaku.getGuard();
                    break;
                }
            }
            if (danmaku.getUid() == 0)
            {
                // qWarning() << "未在已有高能榜上找到，立即更新高能榜";
                // updateOnlineGoldRank();
                return ;
            }

            // 高能榜的，不能有guard，不然会误判为牌子
            // danmaku.setGuardLevel(0);
        }
        else // 舰长进入
        {
            qInfo() << ">>>>>>舰长进入：" << copy_writing;

            QString gd = results.at(1);
            QString uname = results.at(2); // 这个昵称会被系统自动省略（太长后面会是两个点）
            if (us->currentGuards.contains(uid))
                uname = us->currentGuards[uid];
            int guardLevel = 0;
            if (gd == "总督")
                guardLevel = 1;
            else if (gd == "提督")
                guardLevel = 2;
            else if (gd == "舰长")
                guardLevel = 3;

            danmaku = LiveDanmaku(guardLevel, uname, uid, QDateTime::currentDateTime());
        }

        // userComeEvent(danmaku); // 用户进入就有提示了（舰长提示会更频繁）
        triggerCmdEvent(cmd, danmaku.with(data));
    }
    else if (cmd == "WELCOME") // 欢迎老爷，通过vip和svip区分月费和年费老爷
    {
        QJsonObject data = json.value("data").toObject();
        qInfo() << data;
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        bool isAdmin = data.value("isAdmin").toBool();
        qInfo() << s8("欢迎观众：") << username << isAdmin;

        triggerCmdEvent(cmd, LiveDanmaku().with(data));
    }
    else if (cmd == "INTERACT_WORD")
    {
        /* {
            "cmd": "INTERACT_WORD",
            "data": {
                "contribution": {
                    "grade": 0
                },
                "fans_medal": {
                    "anchor_roomid": 0,
                    "guard_level": 0,
                    "icon_id": 0,
                    "is_lighted": 0,
                    "medal_color": 0,
                    "medal_color_border": 12632256,
                    "medal_color_end": 12632256,
                    "medal_color_start": 12632256,
                    "medal_level": 0,
                    "medal_name": "",
                    "score": 0,
                    "special": "",
                    "target_id": 0
                },
                "identities": [
                    1
                ],
                "is_spread": 0,
                "msg_type": 4,
                "roomid": 22639465,
                "score": 1617974941375,
                "spread_desc": "",
                "spread_info": "",
                "tail_icon": 0,
                "timestamp": 1617974941,
                "uid": 20285041,
                "uname": "懒一夕智能科技",
                "uname_color": ""
            }
        } */

        QJsonObject data = json.value("data").toObject();
        int msgType = data.value("msg_type").toInt(); // 1进入直播间，2关注，3分享直播间，4特别关注
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        bool isadmin = data.value("isadmin").toBool(); // TODO:这里没法判断房管？
        QString unameColor = data.value("uname_color").toString();
        bool isSpread = data.value("is_spread").toBool();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        QJsonObject fansMedal = data.value("fans_medal").toObject();
        QString roomId = snum(qint64(data.value("room_id").toDouble()));

        qInfo() << s8("观众交互：") << username << msgType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp), isadmin,
                            unameColor, spreadDesc, spreadInfo);
        danmaku.setMedal(snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())),
                         fansMedal.value("medal_name").toString(),
                         fansMedal.value("medal_level").toInt(),
                         QString("#%1").arg(fansMedal.value("medal_color").toInt(), 6, 16, QLatin1Char('0')),
                         "");

        bool opposite = pking &&
                ((oppositeAudience.contains(uid) && !myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() &&
                     snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())) == pkRoomId));
        danmaku.setOpposite(opposite);
        danmaku.with(data);

        for (const auto& guard: guardInfos)
            if (guard.getUid() == uid)
            {
                danmaku.setGuardLevel(guard.getGuard());
                break;
            }

        if (roomId != "0" && roomId != ac->roomId) // 关注对面主播，也会引发关注事件
        {
            qInfo() << "不是本房间，已忽略：" << roomId << "!=" << ac->roomId;
            return ;
        }

        if (msgType == 1) // 进入直播间
        {
            userComeEvent(danmaku);

            triggerCmdEvent(cmd, danmaku.with(data));
        }
        else if (msgType == 2) // 2关注 4特别关注
        {
            danmaku.transToAttention(timestamp);
            appendNewLiveDanmaku(danmaku);

            if (!justStart && ui->autoSendAttentionCheck->isChecked())
            {
                sendAttentionThankIfNotRobot(danmaku);
            }
            else
            {
                judgeRobotAndMark(danmaku);
            }

            triggerCmdEvent("ATTENTION", danmaku.with(data)); // !这个是单独修改的
        }
        else if (msgType == 3) // 分享直播间
        {
            danmaku.transToShare();
            localNotify(username + "分享了直播间", uid);

            triggerCmdEvent("SHARE", danmaku.with(data));
        }
        else if (msgType == 4) // 特别关注
        {
            danmaku.transToAttention(timestamp);
            danmaku.setSpecial(1);
            appendNewLiveDanmaku(danmaku);

            if (!justStart && ui->autoSendAttentionCheck->isChecked())
            {
                sendAttentionThankIfNotRobot(danmaku);
            }
            else
            {
                judgeRobotAndMark(danmaku);
            }

            triggerCmdEvent("SPECIAL_ATTENTION", danmaku.with(data)); // !这个是单独修改的
        }
        else
        {
            qWarning() << "~~~~~~~~~~~~~~~~~~~~~~~~新的进入msgType" << msgType << json;
        }
    }
    else if (cmd == "ROOM_BLOCK_MSG") // 被禁言
    {
        /*{
            "cmd": "ROOM_BLOCK_MSG",
            "data": {
                "dmscore": 30,
                "operator": 1,
                "uid": 536522379,
                "uname": "sescuerYOKO"
            },
            "uid": 536522379,
            "uname": "sescuerYOKO"
        }*/
        QString nickname = json.value("uname").toString();
        qint64 uid = static_cast<qint64>(json.value("uid").toDouble());
        LiveDanmaku danmaku(LiveDanmaku(nickname, uid));
        appendNewLiveDanmaku(danmaku);
        blockedQueue.append(danmaku);

        triggerCmdEvent(cmd, danmaku.with(json));
    }
    else if (handlePK(json))
    {
    }
    else if (cmd == "GUARD_BUY") // 有人上舰
    {
        // {"end_time":1611343771,"gift_id":10003,"gift_name":"舰长","guard_level":3,"num":1,"price":198000,"start_time":1611343771,"uid":67756641,"username":"31119657605_bili"}
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("username").toString();
        QString giftName = data.value("gift_name").toString();
        int price = data.value("price").toInt();
        int gift_id = data.value("gift_id").toInt();
        int guard_level = data.value("guard_level").toInt();
        int num = data.value("num").toInt();
        // start_time和end_time都是当前时间？
        int guardCount = us->danmakuCounts->value("guard/" + snum(uid), 0).toInt();
        qInfo() << username << s8("购买") << giftName << num << guardCount;
        LiveDanmaku danmaku(username, uid, giftName, num, guard_level, gift_id, price,
                            guardCount == 0 ? 1 : us->currentGuards.contains(uid) ? 0 : 2);
        danmaku.with(data);
        appendNewLiveDanmaku(danmaku);
        appendLiveGuard(danmaku);
        addGuiGiftList(danmaku);

        if (ui->saveEveryGuardCheck->isChecked())
            saveEveryGuard(danmaku);

        // 新船员数量事件
        if (!us->currentGuards.contains(uid))
            newGuardUpdate(danmaku);

        if (!guardCount)
        {
            triggerCmdEvent("FIRST_GUARD", danmaku.with(data), true);
        }
        us->currentGuards[uid] = username;
        guardInfos.append(LiveDanmaku(guard_level, username, uid, QDateTime::currentDateTime()));

        if (!justStart && ui->autoSendGiftCheck->isChecked())
        {
            QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
            if (words.size())
            {
                int r = qrand() % words.size();
                QString msg = words.at(r);
                sendCdMsg(msg, danmaku, NOTIFY_CD, NOTIFY_CD_CN,
                          ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked(), false);
            }
            else if (debugPrint)
            {
                localNotify("[没有可发送的上船弹幕]");
            }
        }

        qint64 userGold = us->danmakuCounts->value("gold/" + snum(uid)).toLongLong();
        userGold += price;
        us->danmakuCounts->setValue("gold/"+snum(uid), userGold);

        int addition = 1;
        if (giftName == "舰长")
            addition = 1;
        else if (giftName == "提督")
            addition = 10;
        else if (giftName == "总督")
            addition = 100;
        guardCount += addition;
        us->danmakuCounts->setValue("guard/" + snum(uid), guardCount);

        dailyGuard += num;
        if (dailySettings)
            dailySettings->setValue("guard", dailyGuard);

        triggerCmdEvent(cmd, danmaku.with(data));
    }
    else if (cmd == "USER_TOAST_MSG") // 续费舰长会附带的；购买不知道
    {
        /*{
            "cmd": "USER_TOAST_MSG",
            "data": {
                "anchor_show": true,
                "color": "#00D1F1",
                "end_time": 1610167490,
                "guard_level": 3,
                "is_show": 0,
                "num": 1,
                "op_type": 3,
                "payflow_id": "2101091244192762134756573",
                "price": 138000,
                "role_name": "舰长",
                "start_time": 1610167490,
                "svga_block": 0,
                "target_guard_count": 128,
                "toast_msg": "<%分说的佛酱%> 自动续费了舰长",
                "uid": 480643475,
                "unit": "月",
                "user_show": true,
                "username": "分说的佛酱"
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "ONLINE_RANK_V2") // 礼物榜（高能榜）更新
    {
        /*{
            "cmd": "ONLINE_RANK_V2",
            "data": {
                "list": [
                    {
                        "face": "http://i0.hdslb.com/bfs/face/5eae154b4ed09c8ae4017325f5fa1ed8fa3757a9.jpg",
                        "guard_level": 3,
                        "rank": 1,
                        "score": "1380",
                        "uid": 480643475,
                        "uname": "分说的佛酱"
                    },
                    {
                        "face": "http://i2.hdslb.com/bfs/face/65536122b97302b86b93847054d4ab8cc155afe3.jpg",
                        "guard_level": 3,
                        "rank": 2,
                        "score": "610",
                        "uid": 407543009,
                        "uname": "布可人"
                    },
                    ..........
                ],
                "rank_type": "gold-rank"
            }
        }*/
        QJsonArray array = json.value("data").toObject().value("list").toArray();
        // 因为高能榜上的只有名字和ID，没有粉丝牌，有需要的话还是需要手动刷新一下
        if (array.size() != onlineGoldRank.size())
        {
            // 还是不更新了吧，没有必要
            // updateOnlineGoldRank();
        }
        else
        {
            // 如果仅仅是排名和金瓜子，那么就挨个修改吧
            foreach (auto val, array)
            {
                auto user = val.toObject();
                qint64 uid = static_cast<qint64>(user.value("uid").toDouble());
                int score = user.value("score").toInt();
                int rank = user.value("rank").toInt();

                for (int i = 0; i < onlineGoldRank.size(); i++)
                {
                    if (onlineGoldRank.at(i).getUid() == uid)
                    {
                        onlineGoldRank[i].setFirst(rank);
                        onlineGoldRank[i].setTotalCoin(score);
                        break;
                    }
                }
            }
        }

        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "ONLINE_RANK_TOP3") // 高能榜前3变化（不知道会不会跟着 ONLINE_RANK_V2）
    {
        /*{
            "cmd": "ONLINE_RANK_TOP3",
            "data": {
                "list": [
                    {
                        "msg": "恭喜 <%分说的佛酱%> 成为高能榜",
                        "rank": 1
                    }
                ]
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "ONLINE_RANK_COUNT") // 高能榜数量变化（但必定会跟着 ONLINE_RANK_V2）
    {
        /*{
            "cmd": "ONLINE_RANK_COUNT",
            "data": {
                "count": 9
            }
        }*/

        // 数量变化了，那还是得刷新一下
        updateOnlineGoldRank();

        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "NOTICE_MSG") // 为什么压缩的消息还有一遍？
    {
        /*{
            "business_id": "",
            "cmd": "NOTICE_MSG",
            "full": {
                "background": "#FFB03CFF",
                "color": "#FFFFFFFF",
                "head_icon": "https://i0.hdslb.com/bfs/live/72337e86020b8d0874d817f15c48a610894b94ff.png",
                "head_icon_fa": "https://i0.hdslb.com/bfs/live/72337e86020b8d0874d817f15c48a610894b94ff.png",
                "head_icon_fan": 1,
                "highlight": "#B25AC1FF",
                "tail_icon": "https://i0.hdslb.com/bfs/live/822da481fdaba986d738db5d8fd469ffa95a8fa1.webp",
                "tail_icon_fa": "https://i0.hdslb.com/bfs/live/38cb2a9f1209b16c0f15162b0b553e3b28d9f16f.png",
                "tail_icon_fan": 4,
                "time": 10
            },
            "half": {
                "background": "",
                "color": "",
                "head_icon": "",
                "highlight": "",
                "tail_icon": "",
                "time": 0
            },
            "link_url": "",
            "msg_common": "",
            "msg_self": "<%分说的佛酱%> 自动续费了主播的 <%舰长%>",
            "msg_type": 3,
            "real_roomid": 12735949,
            "roomid": 12735949,
            "scatter": {
                "max": 0,
                "min": 0
            },
            "shield_uid": -1,
            "side": {
                "background": "#FFE9C8FF",
                "border": "#FFCFA4FF",
                "color": "#EF903AFF",
                "head_icon": "https://i0.hdslb.com/bfs/live/31566d8cd5d468c30de8c148c5d06b3b345d8333.png",
                "highlight": "#D54900FF"
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "SPECIAL_GIFT")
    {
        /*{
            "cmd": "SPECIAL_GIFT",
            "data": {
                "39": {
                    "action": "end",
                    "id": 3032328093737
                }
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "ANCHOR_LOT_CHECKSTATUS") //  开启天选前的审核，审核过了才是真正开启
    {
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "ANCHOR_LOT_START") // 开启天选
    {
        /*{
            "cmd": "ANCHOR_LOT_START",
            "data": {
                "asset_icon": "https://i0.hdslb.com/bfs/live/992c2ccf88d3ea99620fb3a75e672e0abe850e9c.png",
                "award_image": "",
                "award_name": "5.2元红包",
                "award_num": 1,
                "cur_gift_num": 0,
                "current_time": 1610529938,
                "danmu": "娇娇赛高",
                "gift_id": 0,
                "gift_name": "",
                "gift_num": 1,
                "gift_price": 0,
                "goaway_time": 180,
                "goods_id": -99998,
                "id": 773667,
                "is_broadcast": 1,
                "join_type": 0,
                "lot_status": 0,
                "max_time": 600,
                "require_text": "关注主播",
                "require_type": 1, // 1是关注？
                "require_value": 0,
                "room_id": 22532956,
                "send_gift_ensure": 0,
                "show_panel": 1,
                "status": 1,
                "time": 599,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        }*/

        /*{
            "cmd": "ANCHOR_LOT_START",
            "data": {
                "asset_icon": "https://i0.hdslb.com/bfs/live/992c2ccf88d3ea99620fb3a75e672e0abe850e9c.png",
                "award_image": "",
                "award_name": "5.2元红包",
                "award_num": 1,
                "cur_gift_num": 0,
                "current_time": 1610535661,
                "danmu": "我就是天选之人！", // 送礼物的话也是需要发弹幕的（自动发送+自动送礼）
                "gift_id": 20008, // 冰阔落ID
                "gift_name": "冰阔落",
                "gift_num": 1,
                "gift_price": 1000,
                "goaway_time": 180,
                "goods_id": 15, // 物品ID？
                "id": 773836,
                "is_broadcast": 1,
                "join_type": 1,
                "lot_status": 0,
                "max_time": 600,
                "require_text": "无",
                "require_type": 0,
                "require_value": 0,
                "room_id": 22532956,
                "send_gift_ensure": 0,
                "show_panel": 1,
                "status": 1,
                "time": 599,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        }*/

        QJsonObject data = json.value("data").toObject();
        qint64 id = static_cast<qint64>(data.value("id").toDouble());
        QString danmu = data.value("danmu").toString();
        int giftId = data.value("gift_id").toInt();
        int time = data.value("time").toInt();
        qInfo() << "天选弹幕：" << danmu;
        if (!danmu.isEmpty() && giftId <= 0 && ui->autoLOTCheck->isChecked())
        {
            int requireType = data.value("require_type").toInt();
            joinLOT(id, requireType);
        }
        if (danmakuWindow)
        {
            danmakuWindow->addBlockText(danmu);
            QTimer::singleShot(time * 1000, danmakuWindow, [=]{
                danmakuWindow->removeBlockText(danmu);
            });
        }

        triggerCmdEvent(cmd, LiveDanmaku().with(data));
    }
    else if (cmd == "ANCHOR_LOT_END") // 天选结束
    {
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "ANCHOR_LOT_AWARD") // 天选结果推送，在结束后的不到一秒左右
    {
        /* {
            "cmd": "ANCHOR_LOT_AWARD",
            "data": {
                "award_image": "",
                "award_name": "兔の自拍",
                "award_num": 1,
                "award_users": [
                    {
                        "color": 5805790,
                        "face": "http://i1.hdslb.com/bfs/face/f07a981e34985819367eb709baefd80ad5ab4746.jpg",
                        "level": 26,
                        "uid": 44916867,
                        "uname": "-领主-"
                    }
                ],
                "id": 1014952,
                "lot_status": 2,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        } */

        QJsonObject data = json.value("data").toObject();
        QString awardName = data.value("award_name").toString();
        int awardNum = data.value("award_num").toInt();
        QJsonArray awardUsers = data.value("award_users").toArray();
        QStringList names;
        qint64 firstUid = 0;
        foreach (QJsonValue val, awardUsers)
        {
            QJsonObject user = val.toObject();
            QString uname = user.value("uname").toString();
            names.append(uname);
            if (!firstUid)
                firstUid = qint64(user.value("uid").toDouble());
        }

        QString awardRst = awardName + (awardNum > 1 ? "×" + snum(awardNum) : "");
        localNotify("[天选] " + names.join(",") + " 中奖：" + awardRst);

        triggerCmdEvent(cmd, LiveDanmaku(firstUid, names.join(","), awardRst));
    }
    else if (cmd == "VOICE_JOIN_ROOM_COUNT_INFO") // 等待连麦队列数量变化
    {
        /*{
            "cmd": "VOICE_JOIN_ROOM_COUNT_INFO",
            "data": {
                "apply_count": 1, // 猜测：1的话就是添加申请连麦，0是取消申请连麦
                "notify_count": 0,
                "red_point": 0,
                "room_id": 22532956,
                "room_status": 1,
                "root_status": 1
            },
            "roomid": 22532956
        }*/

        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "VOICE_JOIN_LIST") // 连麦申请、取消连麦申请；和VOICE_JOIN_ROOM_COUNT_INFO一起收到
    {
        /*{
            "cmd": "VOICE_JOIN_LIST",
            "data": {
                "apply_count": 1, // 等同于VOICE_JOIN_ROOM_COUNT_INFO的apply_count
                "category": 1,
                "red_point": 1,
                "refresh": 1,
                "room_id": 22532956
            },
            "roomid": 22532956
        }*/

        QJsonObject data = json.value("data").toObject();
        int point = data.value("red_point").toInt();
        localNotify("连麦队列：" + snum(point));

        triggerCmdEvent(cmd, LiveDanmaku().with(data));
    }
    else if (cmd == "VOICE_JOIN_STATUS") // 连麦状态，连麦开始/结束
    {
        /*{
            "cmd": "VOICE_JOIN_STATUS",
            "data": {
                "channel": "voice320168",
                "channel_type": "voice",
                "current_time": 1610802781,
                "guard": 3,
                "head_pic": "http://i1.hdslb.com/bfs/face/5bbf173c5cf4f70481e5814e34bbdf6db564ef80.jpg",
                "room_id": 22532956,
                "start_at": 1610802781,
                "status": 1,                   // 1是开始连麦
                "uid": 1324369,
                "user_name":"\xE6\xB0\xB8\xE8\xBF\x9C\xE5\x8D\x95\xE6\x8E\xA8\xE5\xA8\x87\xE5\xA8\x87\xE7\x9A\x84\xE8\x82\x89\xE5\xA4\xB9\xE9\xA6\x8D",
                "web_share_link": "https://live.bilibili.com/h5/22532956"
            },
            "roomid": 22532956
        }*/
        /*{
            "cmd": "VOICE_JOIN_STATUS",
            "data": {
                "channel": "",
                "channel_type": "voice",
                "current_time": 1610802959,
                "guard": 0,
                "head_pic": "",
                "room_id": 22532956,
                "start_at": 0,
                "status": 0,                   // 0是取消连麦
                "uid": 0,
                "user_name": "",
                "web_share_link": "https://live.bilibili.com/h5/22532956"
            },
            "roomid": 22532956
        }*/

        QJsonObject data = json.value("data").toObject();
        int status = data.value("status").toInt();
        QString uname = data.value("username").toString();
        if (status) // 开始连麦
        {
            if (!uname.isEmpty())
                localNotify((uname + " 接入连麦"));
        }
        else // 取消连麦
        {
            if (!uname.isEmpty())
                localNotify(uname + " 结束连麦");
        }

        triggerCmdEvent(cmd, LiveDanmaku().with(data));
    }
    else if (cmd == "WARNING") // 被警告
    {
        /*{
            "cmd": "WARNING",
            "msg":"违反直播分区规范，未摄像头露脸",
            "roomid": 22532956
        }*/
        QString msg = json.value("msg").toString();
        localNotify(msg);

        triggerCmdEvent(cmd, LiveDanmaku(msg).with(json));
    }
    else if (cmd == "room_admin_entrance")
    {
        /*{
            "cmd": "room_admin_entrance",
            "msg":"系统提示：你已被主播设为房管",
            "uid": 20285041
        }*/
        QString msg = json.value("msg").toString();
        qint64 uid = static_cast<qint64>(json.value("uid").toDouble());
        if (snum(uid) == ac->cookieUid)
        {
            localNotify(msg, uid);
        }
        else
        {
            localNotify(uidToName(uid) + " 被任命为房管", uid);
        }
        triggerCmdEvent(cmd, LiveDanmaku(msg).with(json));
    }
    else if (cmd == "ROOM_ADMIN_REVOKE")
    {
        /*{
            "cmd": "ROOM_ADMIN_REVOKE",
            "msg":"撤销房管",
            "uid": 324495090
        }*/
        QString msg = json.value("msg").toString();
        qint64 uid = static_cast<qint64>(json.value("uid").toDouble());
        localNotify(uidToName(uid) + " 被取消房管", uid);
        triggerCmdEvent(cmd, LiveDanmaku(msg).with(json));
    }
    else if (cmd == "ROOM_ADMINS")
    {
        /*{
            "cmd": "ROOM_ADMINS",
            "uids": [
                36272011, 145884036, 10823381, 67756641, 35001804, 64494330, 295742464, 250439508, 90400995, 384733451, 632481, 41090958, 691018830, 33283612, 13381878, 1324369, 49912988, 2852700, 26472148, 415374297, 20285041
            ]
        }*/

        triggerCmdEvent(cmd, LiveDanmaku().with(json));
    }
    else if (cmd == "LIVE_INTERACTIVE_GAME")
    {
        /*{
            "cmd": "LIVE_INTERACTIVE_GAME",
            "data": {
                "gift_name": "",
                "gift_num": 0,
                "msg": "哈哈哈哈哈哈哈哈哈",
                "price": 0,
                "type": 2,
                "uname": "每天都要学习混凝土"
            }
        }*/
    }
    else if (cmd == "CUT_OFF")
    {
        localNotify("直播间被超管切断");
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "STOP_LIVE_ROOM_LIST")
    {
        return ;
    }
    else if (cmd == "COMMON_NOTICE_DANMAKU") // 大乱斗一堆提示
    {
        /*{
            "cmd": "COMMON_NOTICE_DANMAKU",
            "data": {
                "content_segments": [
                    {
                        "font_color": "#FB7299",
                        "text": "本场PK大乱斗我方获胜！达成<$3$>连胜！感谢<$平平无奇怜怜小天才$>为胜利做出的贡献",
                        "type": 1
                    }
                ],
                "dmscore": 144,
                "terminals": [ 1, 2, 3, 4, 5 ]
            }
        }*/
        MyJson data = json.value("data").toObject();
        QJsonArray array = data.a("content_segments");
        if (array.size() > 0)
        {
            QString text = array.first().toObject().value("text").toString();
            if (text.isEmpty())
            {
                qInfo() << "不含文本的 COMMON_NOTICE_DANMAKU：" << json;
            }
            else
            {
                LiveDanmaku danmaku = LiveDanmaku(text).with(json);
                qInfo() << "NOTICE:" << text;
                if (isFilterRejected("FILTER_DANMAKU_NOTICE", danmaku))
                    return ;
                text.replace("<$", "").replace("$>", "");
                localNotify(text);
                triggerCmdEvent(cmd, danmaku);
            }
        }
        else
        {
            qWarning() << "未处理的 COMMON_NOTICE_DANMAKU：" << json;
        }
    }
    else if (cmd == "WATCHED_CHANGE")
    {
        MyJson data = json.value("data").toObject();
        // int num = data.i("num");
        QString textLarge = data.s("text_large");
        // QString textSmall = data.s("text_small");
        ui->popularityLabel->setToolTip(textLarge);
        ui->popularityTextLabel->setToolTip(textLarge);

        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "VIDEO_CONNECTION_MSG")
    {
        MyJson data = json.value("data").toObject();
        QString channelId = data.s("channel_id");
        qint64 currentTime = data.i("current_time"); // 10位
        int dmscore = data.i("dmscore");
        QString toast = data.s("toast"); // "主播结束了视频连线"
        localNotify(toast);

        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "VIDEO_CONNECTION_JOIN_END") // 视频连线结束，和上面重复的消息（会连续收到许多次）
    {
        MyJson data = json.value("data").toObject();
        QString channelId = data.s("channel_id");
        qint64 currentTime = data.i("current_time"); // 10位
        qint64 startAt = data.i("start_at"); // 10位
        int dmscore = data.i("dmscore");
        QString toast = data.s("toast"); // 主播结束了视频连线
        qint64 roomId = json.value("roomid").toDouble(); // 10位

        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "VIDEO_CONNECTION_JOIN_START") // 视频连线开始（会连续收到许多次）
    {
        MyJson data = json.value("data").toObject();
        QString channelId = data.s("channel_id");
        qint64 currentTime = data.i("current_time"); // 10位
        QString invitedFace = data.s("invitedFace"); // 连线头像URL
        qint64 invitedUid = data.i("invited_uid"); // 连续UID
        QString invitedUname = data.s("invited_uname"); // 连接名字
        qint64 startAt = data.i("start_at"); // 10位
        qint64 roomId = json.value("roomid").toDouble(); // 10位

        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "WATCHED_CHANGE")
    {
        /*{
            "cmd": "WATCHED_CHANGE",
            "data": {
                "num": 83,
                "text_large": "83人看过",
                "text_small": "83"
            }
        }*/
        MyJson data = json.value("data").toObject();
        QString textLarge = data.s("text_large");
        ui->popularityLabel->setToolTip(textLarge);
        ui->popularityTextLabel->setToolTip(textLarge);

        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "DANMU_AGGREGATION") // 弹幕聚合，就是天选之类的弹幕
    {
        /*{
            "cmd": "DANMU_AGGREGATION",
            "data": {
                "activity_identity": "2806688",
                "activity_source": 1,
                "aggregation_cycle": 1,
                "aggregation_icon": "https://i0.hdslb.com/bfs/live/c8fbaa863bf9099c26b491d06f9efe0c20777721.png",
                "aggregation_num": 20,
                "dmscore": 144,
                "msg": "给粗粗写情书",
                "show_rows": 1,
                "show_time": 2,
                "timestamp": 1656331099
            }
        }*/
        MyJson data = json.value("data").toObject();
        QString msg = data.s("msg");

        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "LIVE_OPEN_PLATFORM_GAME") // 开启互动玩法
    {
        LIVE_OPEN_DEB << "互动玩法" << json;
        /*{
            "cmd": "LIVE_OPEN_PLATFORM_GAME",
            "data": {
                "block_uids": null,
                "game_code": "1658282676661",
                "game_conf": "",
                "game_id": "e40cf1c4-ff25-4f60-90a4-9af24ed221c3",
                "game_msg": "",
                "game_name": "神奇弹幕互动版",
                "game_status": "",
                "interactive_panel_conf": "",
                "msg_sub_type": "game_end",
                "msg_type": "game_end",
                "timestamp": 0
            }
        }*/
        MyJson data = json.value("data").toObject();
        QString gameCode = data.s("game_code"); // APP_ID，用来判断是不是本应用的
        QString gameId = data.s("game_id"); // 场次ID

        /* if (liveOpenService && gameCode == snum(liveOpenService->getAppId()))
        {
            liveOpenService->startGame(gameId);
        } */

        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "LIVE_PANEL_CHANGE") // 直播面板改变，已知开启互动玩法后会触发
    {
        LIVE_OPEN_DEB << "互动玩法" << json;
        /*{
            "cmd": "LIVE_PANEL_CHANGE",
            "data": {
                "scatter": {
                    "max": 150,
                    "min": 5
                },
                "type": 2
            }
        }*/
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "PLAY_TOGETHER")
    {
        /*{
            "cmd": "PLAY_TOGETHER",
            "data": {
                "action": "switch_on",
                "apply_number": 0,
                "cur_fleet_num": 0,
                "jump_url": "",
                "max_fleet_num": 0,
                "message": "",
                "message_type": 0,
                "refresh_tool": false,
                "roomid": 24733392,
                "ruid": 1592628332,
                "timestamp": 1658747488,
                "uid": 0,
                "web_url": ""
            } */

        /*{
            "cmd": "PLAY_TOGETHER",
            "data": {
                "action": "switch_on",
                "apply_number": 0,
                "cur_fleet_num": 0,
                "jump_url": "",
                "max_fleet_num": 0,
                "message": "系统提示：主播已切换分区",
                "message_type": 3,
                "refresh_tool": true,
                "roomid": 24733392,
                "ruid": 1592628332,
                "timestamp": 1658747491,
                "uid": 0,
                "web_url": ""
            }
        }*/
        MyJson data = json.value("data").toObject();
        int type = data.i("message_type");
        QString action = data.s("action");
        if (action == "switch_on")
        {
            // TODO:切换分区
        }
        triggerCmdEvent(cmd, data);
    }
    else if (cmd == "POPULARITY_RED_POCKET_START")
    {
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else if (cmd == "POPULARITY_RED_POCKET_WINNER_LIST") // 抽奖红包
    {
        triggerCmdEvent(cmd, LiveDanmaku().with(json.value("data").toObject()));
    }
    else
    {
        qWarning() << "未处理的命令：" << cmd << json;
        triggerCmdEvent(cmd, LiveDanmaku().with(json));
    }
}

void MainWindow::sendWelcomeIfNotRobot(LiveDanmaku danmaku)
{
    if (judgeRobot != 2)
    {
        sendWelcome(danmaku);
        return ;
    }

    judgeUserRobotByFans(danmaku, [=](LiveDanmaku danmaku){
        sendWelcome(danmaku);
    }, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void MainWindow::sendAttentionThankIfNotRobot(LiveDanmaku danmaku)
{
    judgeUserRobotByFans(danmaku, [=](LiveDanmaku danmaku){
        sendAttentionThans(danmaku);
    }, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void MainWindow::judgeUserRobotByFans(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    int val = robotRecord->value("robot/" + snum(danmaku.getUid()), 0).toInt();
    if (val == 1) // 是机器人
    {
        if (ifIs)
            ifIs(danmaku);
        return ;
    }
    else if (val == -1) // 是人
    {
        if (ifNot)
            ifNot(danmaku);
        return ;
    }
    else
    {
        if (danmaku.getMedalLevel() > 0 || danmaku.getLevel() > 1) // 使用等级判断
        {
            robotRecord->setValue("robot/" + snum(danmaku.getUid()), -1);
            if (ifNot)
                ifNot(danmaku);
            return ;
        }
    }

    // 网络判断
    QString url = "http://api.bilibili.com/x/relation/stat?vmid=" + snum(danmaku.getUid());
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            if (code == 403)
                showError("机器人判断粉丝", "您没有权限");
            else
                showError("机器人判断:粉丝", json.value("message").toString());
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int following = obj.value("following").toInt(); // 关注
        int follower = obj.value("follower").toInt(); // 粉丝
        // int whisper = obj.value("whisper").toInt(); // 悄悄关注（自己关注）
        // int black = obj.value("black").toInt(); // 黑名单（自己登录）
        bool robot =  (following >= 100 && follower <= 5) || (follower > 0 && following > follower * 100); // 机器人，或者小号
//        qInfo() << "判断机器人：" << danmaku.getNickname() << "    粉丝数：" << following << follower << robot;
        if (robot)
        {
            // 进一步使用投稿数量判断
            judgeUserRobotByUpstate(danmaku, ifNot, ifIs);
        }
        else // 不是机器人
        {
            robotRecord->setValue("robot/" + snum(danmaku.getUid()), -1);
            if (ifNot)
                ifNot(danmaku);
        }
    });
}

void MainWindow::judgeUserRobotByUpstate(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    QString url = "http://api.bilibili.com/x/space/upstat?mid=" + snum(danmaku.getUid());
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            if (code == 403)
                showError("机器人判断:点击量", "您没有权限");
            else
                showError("机器人判断:点击量", json.value("message").toString());
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int achive_view = obj.value("archive").toObject().value("view").toInt();
        int article_view = obj.value("article").toObject().value("view").toInt();
        int article_like = obj.value("article").toObject().value("like").toInt();
        bool robot = (achive_view + article_view + article_like < 10); // 机器人，或者小号
//        qInfo() << "判断机器人：" << danmaku.getNickname() << "    视频播放量：" << achive_view
//                 << "  专栏阅读量：" << article_view << "  专栏点赞数：" << article_like << robot;
        robotRecord->setValue("robot/" + snum(danmaku.getUid()), robot ? 1 : -1);
        if (robot)
        {
            if (ifIs)
                ifIs(danmaku);
        }
        else // 不是机器人
        {
            if (ifNot)
            {
                ifNot(danmaku);
            }
        }
    });
}

void MainWindow::judgeUserRobotByUpload(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    QString url = "http://api.vc.bilibili.com/link_draw/v1/doc/upload_count?uid=" + snum(danmaku.getUid());
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            if (code == 403)
                showError("机器人判断:投稿", "您没有权限");
            else
                showError("机器人判断:投稿", json.value("message").toString());
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int allCount = obj.value("all_count").toInt();
        bool robot = (allCount <= 1); // 机器人，或者小号（1是因为有默认成为会员的相簿）
        qInfo() << "判断机器人：" << danmaku.getNickname() << "    投稿数量：" << allCount << robot;
        robotRecord->setValue("robot/" + snum(danmaku.getUid()), robot ? 1 : -1);
        if (robot)
        {
            if (ifIs)
                ifIs(danmaku);
        }
        else // 不是机器人
        {
            if (ifNot)
                ifNot(danmaku);
        }
    });
}

void MainWindow::sendWelcome(LiveDanmaku danmaku)
{
    if (us->notWelcomeUsers.contains(danmaku.getUid())
            || (!ui->sendWelcomeTextCheck->isChecked()
            && !ui->sendWelcomeVoiceCheck->isChecked())) // 不自动欢迎
        return ;
    QStringList words = getEditConditionStringList(ui->autoWelcomeWordsEdit->toPlainText(), danmaku);
    if (!words.size())
    {
        if (debugPrint)
            localNotify("[没有可用的欢迎弹幕]");
        return ;
    }
    int r = qrand() % words.size();
    if (debugPrint && !(words.size() == 1 && words.first().trimmed().isEmpty()))
        localNotify("[rand " + snum(r) + " in " + snum(words.size()) + "]");
    QString msg = words.at(r);
    if (us->strongNotifyUsers.contains(danmaku.getUid()))
    {
        if (debugPrint)
            localNotify("[强提醒]");
        sendCdMsg(msg, danmaku, 2000, NOTIFY_CD_CN,
                  ui->sendWelcomeTextCheck->isChecked(), ui->sendWelcomeVoiceCheck->isChecked(), false);
    }
    else
    {
        sendCdMsg(msg, danmaku, ui->sendWelcomeCDSpin->value() * 1000, WELCOME_CD_CN,
                  ui->sendWelcomeTextCheck->isChecked(), ui->sendWelcomeVoiceCheck->isChecked(), false);
    }
}

void MainWindow::sendAttentionThans(LiveDanmaku danmaku)
{
    QStringList words = getEditConditionStringList(ui->autoAttentionWordsEdit->toPlainText(), danmaku);
    if (!words.size())
    {
        if (debugPrint)
            localNotify("[没有可用的感谢关注弹幕]");
        return ;
    }
    int r = qrand() % words.size();
    QString msg = words.at(r);
    sendAttentionMsg(msg, danmaku);
}

void MainWindow::judgeRobotAndMark(LiveDanmaku danmaku)
{
    if (!judgeRobot)
        return ;
    judgeUserRobotByFans(danmaku, nullptr, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void MainWindow::markNotRobot(qint64 uid)
{
    if (!judgeRobot)
        return ;
    int val = robotRecord->value("robot/" + snum(uid), 0).toInt();
    if (val != -1)
        robotRecord->setValue("robot/" + snum(uid), -1);
}

void MainWindow::initTTS()
{
    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (!tts)
        {
            qInfo() << "初始化TTS语音模块";
            tts = new QTextToSpeech(this);
            tts->setRate( (voiceSpeed = us->value("voice/speed", 50).toInt() - 50) / 50.0 );
            tts->setPitch( (voicePitch = us->value("voice/pitch", 50).toInt() - 50) / 50.0 );
            tts->setVolume( (voiceVolume = us->value("voice/volume", 50).toInt()) / 100.0 );
            connect(tts, &QTextToSpeech::stateChanged, this, [=](QTextToSpeech::State state){
                if (state == QTextToSpeech::Ready)
                    speakTextQueueNext();
            });
        }
#endif
        break;
    case VoiceXfy:
        if (!xfyTTS)
        {
            qInfo() << "初始化讯飞语音模块";
            xfyTTS = new XfyTTS(rt->dataPath,
                                us->value("xfytts/appid").toString(),
                                us->value("xfytts/apikey").toString(),
                                us->value("xfytts/apisecret").toString(),
                                this);
            ui->xfyAppIdEdit->setText(us->value("xfytts/appid").toString());
            ui->xfyApiKeyEdit->setText(us->value("xfytts/apikey").toString());
            ui->xfyApiSecretEdit->setText(us->value("xfytts/apisecret").toString());
            xfyTTS->setName( voiceName = us->value("xfytts/name", "xiaoyan").toString() );
            xfyTTS->setPitch( voicePitch = us->value("voice/pitch", 50).toInt() );
            xfyTTS->setSpeed( voiceSpeed = us->value("voice/speed", 50).toInt() );
            xfyTTS->setVolume( voiceSpeed = us->value("voice/speed", 50).toInt() );
        }
        break;
    case VoiceMS:
        if (!msTTS)
        {
            qInfo() << "初始化微软语音模块";
            msTTS = new MicrosoftTTS(rt->dataPath,
                                     us->value("mstts/areaCode").toString(),
                                     us->value("mstts/subscriptionKey").toString(),
                                     this);
            connect(msTTS, &MicrosoftTTS::signalError, this, [=](QString err) {
                showError("微软语音", err);
            });
            ui->MSAreaCodeEdit->setText(us->value("mstts/areaCode").toString());
            ui->MSSubscriptionKeyEdit->setText(us->value("mstts/subscriptionKey").toString());
            msTTSFormat = us->value("mstts/format", DEFAULT_MS_TTS_SSML_FORMAT).toString();
            // TODO: 设置音调等内容
        }
        break;
    case VoiceCustom:
        break;
    }
}

void MainWindow::speekVariantText(QString text)
{
    if (!shallSpeakText())
        return ;

    // 开始播放
    speakText(text);
}

void MainWindow::speakText(QString text)
{
    // 处理特殊字符
    text.replace("_", " ");

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (!tts)
            initTTS();
        else if (tts->state() != QTextToSpeech::Ready)
        {
            ttsQueue.append(text);
            return ;
        }
        tts->say(text);
#endif
        break;
    case VoiceXfy:
        if (!xfyTTS)
            initTTS();
        xfyTTS->speakText(text);
        break;
    case VoiceMS:
    {
        if (!msTTS)
            initTTS();

        // 替换文本
        if (!text.startsWith("<speak"))
        {
            QString rpl = msTTSFormat;
            text = rpl.replace("%text%", text);
        }
        msTTS->speakSSML(text);
    }
        break;
    case VoiceCustom:
        if (ttsDownloading || (ttsPlayer && ttsPlayer->state() == QMediaPlayer::State::PlayingState))
        {
            ttsQueue.append(text);
        }
        else
        {
            voiceDownloadAndSpeak(text);
        }
        break;
    }
}

void MainWindow::speakTextQueueNext()
{
    if (!ttsQueue.size())
        return ;
    QString text = ttsQueue.takeFirst();
    speakText(text);
}

/**
 * 快速设置自定义语音的类型
 */
void MainWindow::on_setCustomVoiceButton_clicked()
{
    QStringList names{"测试",
                     "百度语音"};
    QStringList urls{
        "http://120.24.87.124/cgi-bin/ekho2.pl?cmd=SPEAK&voice=EkhoMandarin&speedDelta=0&pitchDelta=0&volumeDelta=0&text=%url_text%",
        "https://cloud.baidu.com/aidemo?type=tns&per=4119&spd=6&pit=5&vol=5&aue=3&tex=%url_text%"
    };
    QStringList descs{
        "",
        "spd：语速，0~15，默认为5中语速\n\
pit：音调，0~15，默认为5中音调\n\
vol：音量，0~15，默认为5中音量（取0时为音量最小值，并非静音）\n\
per：（基础）0 度小美，1 度小宇，3 度逍遥，4 度丫丫，4115 度小贤，4105 度灵儿，4117 度小乔，4100 度小雯\n\
per：（精品）5003 度逍遥，5118 度小鹿，106 度博文，110 度小童，111 度小萌，103 度米朵，5 度小娇\n\
aue：3为mp3（必须）"
    };

    newFacileMenu;
    for (int i = 0; i < names.size(); i++)
    {
        menu->addAction(names.at(i), [=]{
            ui->voiceCustomUrlEdit->setText(urls.at(i));
            on_voiceCustomUrlEdit_editingFinished();
        })->tooltip(descs.at(i));
    }
    menu->addTitle("鼠标悬浮可查看参数");
    menu->exec();
}


/**
 * 针对返回是JSON/音频字节的类型
 * 自动判断
 */
void MainWindow::voiceDownloadAndSpeak(QString text)
{
    QString url = ui->voiceCustomUrlEdit->text();
    if (url.isEmpty())
        return ;
    url = url.replace("%1", text).replace("%text%", text);
    url = url.replace("%url_text%", QString::fromUtf8(text.toUtf8().toPercentEncoding()));
    playNetAudio(url);
}

void MainWindow::playNetAudio(QString url)
{
    qInfo() << "播放网络音频：" << url;
    const QString filePath = rt->dataPath + "tts";
    QDir dir(filePath);
    dir.mkpath(filePath);

    ttsDownloading = true;
    get(url, [=](QNetworkReply* reply1){
        ttsDownloading = false;
        QByteArray fileData;

        if (reply1->error() != QNetworkReply::NoError)
        {
            qWarning() << "获取网络音频错误：" << reply1->errorString();
            return ;
        }

        auto contentType = reply1->header(QNetworkRequest::ContentTypeHeader).toString();
        if (contentType.contains("json"))
        {
            // https://cloud.baidu.com/aidemo?type=tns&per=4119&spd=6&pit=5&vol=5&aue=3&tex=这是一个测试文本
            QByteArray ba = reply1->readAll();
            MyJson json(ba);
            QString s = json.s("data");
            if (s.startsWith("data:audio") && s.contains(","))
            {
                QStringList sl = s.split(",");
                fileData = sl.at(1).toUtf8();
                if (sl.at(0).contains("base64"))
                    fileData = QByteArray::fromBase64(fileData);
            }
            else
            {
                qWarning() << "无法解析的语音返回：" << json;
                showError("在线音频", json.errOrMsg());
                return ;
            }
        }
        else
        {
            // http://120.24.87.124/cgi-bin/ekho2.pl?cmd=SPEAK&voice=EkhoMandarin&speedDelta=0&pitchDelta=0&volumeDelta=0&text=这是一个测试文本
            fileData = reply1->readAll();
        }

        if (fileData.isEmpty())
        {
            qWarning() << "网络音频为空";
            return ;
        }

        // 保存文件
        QString path = filePath + "/" + snum(QDateTime::currentMSecsSinceEpoch()) + ".mp3";
        QFile file(path);
        file.open(QFile::WriteOnly);
        QDataStream stream(&file);
        stream << fileData;
        file.flush();
        file.close();

        // 播放文件
        if (!ttsPlayer)
        {
            ttsPlayer = new QMediaPlayer(this);
            connect(ttsPlayer, &QMediaPlayer::stateChanged, this, [=](QMediaPlayer::State state){
                if (state == QMediaPlayer::StoppedState)
                {
                    QFile file(path);
                    file.remove();
                    speakTextQueueNext();
                }
            });
        }
        ttsPlayer->setMedia(QUrl::fromLocalFile(path));

        ttsPlayer->play();
    });
}

void MainWindow::showScreenDanmaku(const LiveDanmaku &danmaku)
{
    if (!ui->enableScreenDanmakuCheck->isChecked()) // 不显示所有弹幕
        return ;
    if (!ui->enableScreenMsgCheck->isChecked() && danmaku.getMsgType() != MSG_DANMAKU) // 不显示所有msg
        return ;
    if (_loadingOldDanmakus) // 正在加载旧弹幕
        return ;
    if (danmaku.isPkLink()) // 对面同步过来的弹幕
        return ;

    // 显示动画
    QLabel* label = new QLabel(nullptr);
    label->setWindowFlag(Qt::FramelessWindowHint, true);
    label->setWindowFlag(Qt::Tool, true);
    label->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    label->setAttribute(Qt::WA_TranslucentBackground, true); // 设置窗口透明
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    QString disp;
    if (danmaku.getMsgType() == MSG_DANMAKU || !danmaku.getText().isEmpty())
    {
        disp = danmaku.getText();
        if (ui->screenDanmakuWithNameCheck->isChecked())
            disp = danmaku.getNickname() + "：" + disp;
    }
    else
    {
        QString text = danmaku.toString();
        QRegularExpression re("^\\d+:\\d+:\\d+\\s+(.+)$"); // 简化表达式，去掉时间信息
        QRegularExpressionMatch match;
        if (text.indexOf(re, 0, &match) > -1)
            text = match.capturedTexts().at(1);
        disp = text;
    }
    label->setText(disp);
    label->setFont(screenDanmakuFont);

    auto isBlankColor = [=](QString c) -> bool {
        c = c.toLower();
        return c.isEmpty() || c == "#ffffff" || c == "#000000"
                || c == "#ffffffff" || c == "#00000000";
    };
    if (!danmaku.getTextColor().isEmpty() && !isBlankColor(danmaku.getTextColor()))
        label->setStyleSheet("color:" + danmaku.getTextColor() + ";");
    else
        label->setStyleSheet("color:" + QVariant(screenDanmakuColor).toString() + ";");
    label->adjustSize();
    label->show();

    QRect rect = getScreenRect();
    int left = rect.left() + rect.width() * ui->screenDanmakuLeftSpin->value() / 100;
    int right = rect.right() - rect.width() * ui->screenDanmakuRightSpin->value() / 100;
    int top = rect.top() + rect.height() * ui->screenDanmakuTopSpin->value() / 100;
    int bottom = rect.bottom() - rect.height() * ui->screenDanmakuBottomSpin->value() / 100;
    int sx = right, ex = left - label->width();
    if (left > right) // 从左往右
    {
        sx = right - label->width();
        ex = left;
    }

    // 避免重叠
    auto isOverlap = [=](int x, int y, int height){
        foreach (QLabel* label, screenLabels)
        {
            if (label->geometry().contains(x, y)
                    || label->geometry().contains(x, y+height))
                return true;
        }
        return false;
    };
    int forCount = 0;
    int y = 0;
    do {
        int ry = qrand() % (qMax(30, qAbs(bottom - top) - label->height()));
        y = qMin(top, bottom) + label->height() + ry;
        if (++forCount >= 32) // 最多循环32遍（如果弹幕极多，可能会必定叠在一起）
            break;
    } while (isOverlap(sx, y, label->height()));

    label->move(QPoint(sx, y));
    screenLabels.append(label);
    QPropertyAnimation* ani = new QPropertyAnimation(label, "pos");
    ani->setStartValue(QPoint(sx, y));
    ani->setEndValue(QPoint(ex, y));
    ani->setDuration(ui->screenDanmakuSpeedSpin->value() * 1000);
    connect(ani, &QPropertyAnimation::finished, ani, [=]{
        ani->deleteLater();
        screenLabels.removeOne(label);
        label->deleteLater();
    });
    ani->start();
}

/**
 * 合并消息
 * 在添加到消息队列前调用此函数判断
 * 若是，则同步合并实时弹幕中的礼物连击
 */
bool MainWindow::mergeGiftCombo(const LiveDanmaku &danmaku)
{
    if (danmaku.getMsgType() != MSG_GIFT)
        return false;

    // 判断，同人 && 礼物同名 && x秒内
    qint64 uid = danmaku.getUid();
    int giftId = danmaku.getGiftId();
    qint64 time = danmaku.getTimeline().toSecsSinceEpoch();
    LiveDanmaku* merged = nullptr;
    int delayTime = ui->giftComboDelaySpin->value(); // + 1; // 多出的1秒当做网络延迟了

    // 遍历房间弹幕
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        LiveDanmaku dm = roomDanmakus.at(i);
        qint64 t = dm.getTimeline().toSecsSinceEpoch();
        if (t == 0) // 有些是没带时间的
            continue;
        if (t + delayTime < time) // x秒以内
            return false;
        if (dm.getMsgType() != MSG_GIFT
                || dm.getUid() != uid
                || dm.getGiftId() != giftId)
            continue;

        // 是这个没错了
        merged = &roomDanmakus[i];
        break;
    }
    if (!merged)
        return false;

    // 开始合并
    qInfo() << "合并相同礼物至：" << merged->toString();
    merged->addGift(danmaku.getNumber(), danmaku.getTotalCoin(), danmaku.getTimeline());

    // 合并实时弹幕
    if (danmakuWindow)
        danmakuWindow->mergeGift(danmaku, delayTime);

    return true;
}

bool MainWindow::handlePK(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();

    if (cmd == "PK_BATTLE_PRE") // 开始前的等待状态
    {
        return true;
    }
    else if (cmd == "PK_BATTLE_PRE_NEW")
    {
        /*{
            "cmd": "PK_BATTLE_PRE_NEW",
            "data": {
                "battle_type": 1,
                "end_win_task": null,
                "face": "http://i0.hdslb.com/bfs/face/4c0e444dbabe86a3c4a3c47b72e2e63bd4a96684.jpg",
                "match_type": 1,
                "pk_votes_name":"\xE4\xB9\xB1\xE6\x96\x97\xE5\x80\xBC",
                "pre_timer": 10,
                "room_id": 4857111,
                "season_id": 31,
                "uid": 14833326,
                "uname":"\xE5\x8D\x83\xE9\xAD\x82\xE5\x8D\xB0"
            },
            "pk_id": 200271102,
            "pk_status": 101,
            "roomid": 22532956,
            "timestamp": 1611152119
        }*/
        pkPre(json);
        triggerCmdEvent("PK_BATTLE_PRE", LiveDanmaku(json));
        return true;
    }
    if (cmd == "PK_BATTLE_START") // 开始大乱斗
    {
        return true;
    }
    else if (cmd == "PK_BATTLE_START_NEW")
    {
        /*{
            "cmd": "PK_BATTLE_START_NEW",
            "pk_id": 200271102,
            "pk_status": 201,
            "timestamp": 1611152129,
            "data": {
                "battle_type": 1,
                "final_hit_votes": 0,
                "pk_start_time": 1611152129,
                "pk_frozen_time": 1611152429,
                "pk_end_time": 1611152439,
                "pk_votes_type": 0,
                "pk_votes_add": 0,
                "pk_votes_name": "\\u4e71\\u6597\\u503c"
            }
        }*/
        pkStart(json);
        triggerCmdEvent("PK_BATTLE_START", LiveDanmaku(json));
        return true;
    }
    else if (cmd == "PK_BATTLE_PROCESS") // 双方送礼信息
    {
        return true;
    }
    else if (cmd == "PK_BATTLE_PROCESS_NEW")
    {
        /*{
            "cmd": "PK_BATTLE_PROCESS_NEW",
            "pk_id": 200270835,
            "pk_status": 201,
            "timestamp": 1611151874,
            "data": {
                "battle_type": 1,
                "init_info": {
                    "room_id": 2603963,
                    "votes": 55,
                    "best_uname": "\\u963f\\u5179\\u963f\\u5179\\u7684\\u77db"
                },
                "match_info": {
                    "room_id": 22532956,
                    "votes": 184,
                    "best_uname": "\\u591c\\u7a7a\\u3001"
                }
            }
        }*/
        pkProcess(json);
        triggerCmdEvent("PK_BATTLE_PROCESS", LiveDanmaku(json));
        return true;
    }
    else if (cmd == "PK_BATTLE_RANK_CHANGE")
    {
        /*{
            "cmd": "PK_BATTLE_RANK_CHANGE",
            "timestamp": 1611152461,
            "data": {
                "first_rank_img_url": "https:\\/\\/i0.hdslb.com\\/bfs\\/live\\/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                "rank_name": "\\u767d\\u94f6\\u6597\\u58ebx1\\u661f"
            }
        }*/
    }
    else if (cmd == "PK_BATTLE_FINAL_PROCESS") // 绝杀时刻开始：3分钟~4分钟
    {
        /*{
            "cmd": "PK_BATTLE_FINAL_PROCESS",
            "data": {
                "battle_type": 2, // 1
                "pk_frozen_time": 1628089118
            },
            "pk_id": 205052526,
            "pk_status": 301,     // 201
            "timestamp": 1628088939
        }*/

        triggerCmdEvent("PK_BATTLE_FINAL_PROCESS", LiveDanmaku(json));
        return true;
    }
    else if (cmd == "PK_BATTLE_END") // 结束信息
    {
        /**
         * 结束事件依次是：
         * 1. PK_BATTLE_END（触发 PK_BEST_UNAME & PK_END）
         * 2. PK_BATTLE_SETTLE_USER
         * 3. PK_BATTLE_SETTLE_V2
         */
        pkEnd(json);
    }
    else if (cmd == "PK_BATTLE_SETTLE") // 这个才是真正的PK结束消息！
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE",
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006,
            "data": {
                "battle_type": 1,
                "result_type": 2
            },
            "roomid": "22532956"
        }*/
        // result_type: 2赢，-1输

        // 因为这个不只是data，比较特殊
        // triggerCmdEvent(cmd, LiveDanmaku().with(json));
        return true;
    }
    else if (cmd == "PK_BATTLE_SETTLE_NEW")
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE_NEW",
            "pk_id": 200933662,
            "pk_status": 601,
            "timestamp": 1613959764,
            "data": {
                "pk_id": 200933662,
                "pk_status": 601,
                "settle_status": 1,
                "punish_end_time": 1613959944,
                "timestamp": 1613959764,
                "battle_type": 6,
                "init_info": {
                    "room_id": 7259049,
                    "result_type": -1,
                    "votes": 0,
                    "assist_info": []
                },
                "match_info": {
                    "room_id": 21839758,
                    "result_type": 2,
                    "votes": 3,
                    "assist_info": [
                        {
                            "rank": 1,
                            "uid": 412357310,
                            "face": "http:\\/\\/i0.hdslb.com\\/bfs\\/face\\/e97fbf0e412b936763033055821e1ff5df56565a.jpg",
                            "uname": "\\u6cab\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8"
                        }
                    ]
                },
                "dm_conf": {
                    "font_color": "#FFE10B",
                    "bg_color": "#72C5E2"
                }
            }
        }*/
        pkSettle(json);
        triggerCmdEvent("PK_BATTLE_SETTLE", LiveDanmaku(json));
        return true;
    }
    else if (cmd == "PK_BATTLE_SETTLE_USER")
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE_USER",
            "data": {
                "battle_type": 0,
                "level_info": {
                    "first_rank_img": "https://i0.hdslb.com/bfs/live/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                    "first_rank_name": "白银斗士",
                    "second_rank_icon": "https://i0.hdslb.com/bfs/live/1f8c2a959f92592407514a1afeb705ddc55429cd.png",
                    "second_rank_num": 1
                },
                "my_info": {
                    "best_user": {
                        "award_info": null,
                        "award_info_list": [],
                        "badge": {
                            "desc": "",
                            "position": 0,
                            "url": ""
                        },
                        "end_win_award_info_list": {
                            "list": []
                        },
                        "exp": {
                            "color": 6406234,
                            "level": 1
                        },
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                        "pk_votes": 10,
                        "pk_votes_name": "乱斗值",
                        "uid": 64494330,
                        "uname": "我今天超可爱0"
                    },
                    "exp": {
                        "color": 9868950,
                        "master_level": {
                            "color": 5805790,
                            "level": 17
                        },
                        "user_level": 2
                    },
                    "face": "http://i2.hdslb.com/bfs/face/5b96b6ba5b078001e8159406710a8326d67cee5c.jpg",
                    "face_frame": "https://i0.hdslb.com/bfs/vc/d186b7d67d39e0894ebcc7f3ca5b35b3b56d5192.png",
                    "room_id": 22532956,
                    "uid": 688893202,
                    "uname": "娇娇子er"
                },
                "pk_id": "100729259",
                "result_info": {
                    "pk_crit_score": -1,
                    "pk_done_times": 17,
                    "pk_extra_score": 0,
                    "pk_extra_score_slot": "",
                    "pk_extra_value": 0,
                    "pk_resist_crit_score": -1,
                    "pk_task_score": 0,
                    "pk_times_score": 0,
                    "pk_total_times": -1,
                    "pk_votes": 10,
                    "pk_votes_name": "乱斗值",
                    "result_type_score": 12,
                    "task_score_list": [],
                    "total_score": 12,
                    "win_count": 2,
                    "win_final_hit": -1,
                    "winner_count_score": 0
                },
                "result_type": "2",
                "season_id": 29,
                "settle_status": 1,
                "winner": {
                    "best_user": {
                        "award_info": null,
                        "award_info_list": [],
                        "badge": {
                            "desc": "",
                            "position": 0,
                            "url": ""
                        },
                        "end_win_award_info_list": {
                            "list": []
                        },
                        "exp": {
                            "color": 6406234,
                            "level": 1
                        },
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                        "pk_votes": 10,
                        "pk_votes_name": "乱斗值",
                        "uid": 64494330,
                        "uname": "我今天超可爱0"
                    },
                    "exp": {
                        "color": 9868950,
                        "master_level": {
                            "color": 5805790,
                            "level": 17
                        },
                        "user_level": 2
                    },
                    "face": "http://i2.hdslb.com/bfs/face/5b96b6ba5b078001e8159406710a8326d67cee5c.jpg",
                    "face_frame": "https://i0.hdslb.com/bfs/vc/d186b7d67d39e0894ebcc7f3ca5b35b3b56d5192.png",
                    "room_id": 22532956,
                    "uid": 688893202,
                    "uname": "娇娇子er"
                }
            },
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006
        }*/
        // PK结束更新大乱斗信息
        upgradeWinningStreak(true);
        return true;
    }
    else if (cmd == "PK_BATTLE_SETTLE_V2")
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE_V2",
            "data": {
                "assist_list": [
                    {
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "id": 64494330,
                        "score": 10,
                        "uname": "我今天超可爱0"
                    }
                ],
                "level_info": {
                    "first_rank_img": "https://i0.hdslb.com/bfs/live/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                    "first_rank_name": "白银斗士",
                    "second_rank_icon": "https://i0.hdslb.com/bfs/live/1f8c2a959f92592407514a1afeb705ddc55429cd.png",
                    "second_rank_num": 1,
                    "uid": "688893202"
                },
                "pk_id": "100729259",
                "pk_type": "1",
                "result_info": {
                    "pk_extra_value": 0,
                    "pk_votes": 10,
                    "pk_votes_name": "乱斗值",
                    "total_score": 12
                },
                "result_type": 2,
                "season_id": 29
            },
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006
        }*/
        return true;
    }
    else if (cmd == "PK_BATTLE_PUNISH_END")
    {
        /* {
            "cmd": "PK_BATTLE_PUNISH_END",
            "pk_id": "203882854",
            "pk_status": 1001,
            "status_msg": "",
            "timestamp": 1624466237,
            "data": {
                "battle_type": 6
            }
        } */
    }
    else if (cmd == "PK_LOTTERY_START") // 大乱斗胜利后的抽奖，触发未知，实测在某次大乱斗送天空之翼后有
    {
        /*{
            "cmd": "PK_LOTTERY_START",
            "data": {
                "asset_animation_pic": "https://i0.hdslb.com/bfs/vc/03be4c2912a4bd9f29eca3dac059c0e3e3fc69ce.gif",
                "asset_icon": "https://i0.hdslb.com/bfs/vc/44c367b09a8271afa22853785849e65797e085a1.png",
                "from_user": {
                    "face": "http://i2.hdslb.com/bfs/face/f25b706762e00a9adfe13e6147650891dd6f69a0.jpg",
                    "uid": 688893202,
                    "uname": "娇娇子er"
                },
                "id": 200105856,
                "max_time": 120,
                "pk_id": 200105856,
                "room_id": 22532956,
                "thank_text": "恭喜<%娇娇子er%>赢得大乱斗PK胜利",
                "time": 120,
                "time_wait": 0,
                "title": "恭喜主播大乱斗胜利",
                "weight": 0
            }
        }*/
    }
    else
    {
        return false;
    }

    triggerCmdEvent(cmd, LiveDanmaku(json));
    return true;
}

void MainWindow::userComeEvent(LiveDanmaku &danmaku)
{
    qint64 uid = danmaku.getUid();

    // [%come_time% > %timestamp%-3600]*%ai_name%，你回来了~ // 一小时内
    // [%come_time%>0, %come_time%<%timestamp%-3600*24]*%ai_name%，你终于来喽！
    int userCome = us->danmakuCounts->value("come/" + snum(uid)).toInt();
    danmaku.setNumber(userCome);
    danmaku.setPrevTimestamp(us->danmakuCounts->value("comeTime/"+snum(uid), 0).toLongLong());

    appendNewLiveDanmaku(danmaku);

    userCome++;
    us->danmakuCounts->setValue("come/"+snum(uid), userCome);
    us->danmakuCounts->setValue("comeTime/"+snum(uid), danmaku.getTimeline().toSecsSinceEpoch());

    dailyCome++;
    if (dailySettings)
        dailySettings->setValue("come", dailyCome);
    if (danmaku.isOpposite())
    {
        // 加到自己这边来，免得下次误杀（即只提醒一次）
        // 不过不能这么做，否则不会显示“对面”两个字了
        // myAudience.insert(uid);
    }
    else if (cmAudience.contains(uid))
    {
        if (cmAudience.value(uid) > 0)
        {
            danmaku.setViewReturn(true);
            danmaku.setPrevTimestamp(cmAudience[uid]);
            cmAudience[uid] = 0; // 标记为串门回来
            localNotify(danmaku.getNickname() + " 去对面串门回来");
        }
    }

    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    if (!justStart && ui->autoSendWelcomeCheck->isChecked()) // 发送欢迎
    {
        us->userComeTimes[uid] = currentTime;
        sendWelcomeIfNotRobot(danmaku);
    }
    else // 不发送欢迎，只是查看
    {
        us->userComeTimes[uid] = currentTime; // 直接更新了
        if (judgeRobot == 2)
        {
            judgeRobotAndMark(danmaku);
        }
    }
}

void MainWindow::refreshBlockList()
{
    if (ac->browserData.isEmpty())
    {
        showError("请先设置用户数据");
        return ;
    }

    // 刷新被禁言的列表
    QString url = "https://api.live.bilibili.com/liveact/ajaxGetBlockList?roomid="+ac->roomId+"&page=1";
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            qWarning() << "获取禁言：" << json.value("message").toString();
            /* if (code == 403)
                showError("获取禁言", "您没有权限");
            else
                showError("获取禁言", json.value("message").toString()); */
            return ;
        }
        QJsonArray list = json.value("data").toArray();
        us->userBlockIds.clear();
        foreach (QJsonValue val, list)
        {
            QJsonObject obj = val.toObject();
            qint64 id = static_cast<qint64>(obj.value("id").toDouble());
            qint64 uid = static_cast<qint64>(obj.value("uid").toDouble());
            QString uname = obj.value("uname").toString();
            us->userBlockIds.insert(uid, id);
//            qInfo() << "已屏蔽:" << id << uname << uid;
        }

    });
}

bool MainWindow::isInFans(qint64 uid)
{
    foreach (auto fan, fansList)
    {
        if (fan.mid == uid)
            return true;
    }
    return false;
}

void MainWindow::sendGift(int giftId, int giftNum)
{
    if (ac->roomId.isEmpty() || ac->browserCookie.isEmpty())
    {
        qWarning() << "房间为空，或未登录";
        return ;
    }

    if (localDebug)
    {
        localNotify("赠送礼物 -> " + snum(giftId) + " x " + snum(giftNum));
        return ;
    }


    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "uid=" + ac->cookieUid;
    datas << "gift_id=" + snum(giftId);
    datas << "ruid=" + ac->upUid;
    datas << "send_ruid=0";
    datas << "gift_num=" + snum(giftNum);
    datas << "coin_type=gold";
    datas << "bag_id=0";
    datas << "platform=pc";
    datas << "biz_code=live";
    datas << "biz_id=" + ac->roomId;
    datas << "rnd=" + snum(QDateTime::currentSecsSinceEpoch());
    datas << "storm_beat_id=0";
    datas << "metadata=";
    datas << "price=0";
    datas << "csrf_token=" + ac->csrf_token;
    datas << "csrf=" + ac->csrf_token;
    datas << "visit_id=";

    QByteArray ba(datas.join("&").toStdString().data());

    QString url("https://api.live.bilibili.com/gift/v2/Live/send");
    post(url, ba, [=](QJsonObject json){
        QString message = json.value("message").toString();
        statusLabel->setText("");
        if (message != "success")
        {
            showError("赠送礼物", message);
        }
    });
}

void MainWindow::sendBagGift(int giftId, int giftNum, qint64 bagId)
{
    if (ac->roomId.isEmpty() || ac->browserCookie.isEmpty())
    {
        qWarning() << "房间为空，或未登录";
        return ;
    }

    if (localDebug)
    {
        localNotify("赠送包裹礼物 -> " + snum(giftId) + " x " + snum(giftNum));
        return ;
    }

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "uid=" + ac->cookieUid;
    datas << "gift_id=" + snum(giftId);
    datas << "ruid=" + ac->upUid;
    datas << "send_ruid=0";
    datas << "gift_num=" + snum(giftNum);
    datas << "bag_id=" + snum(bagId);
    datas << "platform=pc";
    datas << "biz_code=live";
    datas << "biz_id=" + ac->roomId;
    datas << "rnd=" + snum(QDateTime::currentSecsSinceEpoch());
    datas << "storm_beat_id=0";
    datas << "metadata=";
    datas << "price=0";
    datas << "csrf_token=" + ac->csrf_token;
    datas << "csrf=" + ac->csrf_token;
    datas << "visit_id=";

    QByteArray ba(datas.join("&").toStdString().data());

    QString url("https://api.live.bilibili.com/gift/v2/live/bag_send");
    post(url, ba, [=](QJsonObject json){
        QString message = json.value("message").toString();
        statusLabel->setText("");
        if (message != "success")
        {
            showError("赠送包裹礼物失败", message);
            qCritical() << s8("warning: 发送失败：") << message << datas.join("&");
        }
    });
}

void MainWindow::getRoomLiveVideoUrl(StringFunc func)
{
    if (ac->roomId.isEmpty())
        return ;
    /*QString url = "https://api.live.bilibili.com/room/v1/Room/playUrl?cid=" + ac->roomId
            + "&quality=4&qn=10000&platform=web&otype=json";
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("获取视频URL返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取链接
        QJsonArray array = json.value("data").toObject().value("durl").toArray();
        /*"url": "http://d1--cn-gotcha04.bilivideo.com/live-bvc/521719/live_688893202_7694436.flv?cdn=cn-gotcha04\\u0026expires=1607505058\\u0026len=0\\u0026oi=1944862322\\u0026pt=\\u0026qn=10000\\u0026trid=40938d869aca4cb39041730f74ff9051\\u0026sigparams=cdn,expires,len,oi,pt,qn,trid\\u0026sign=ac701ba60c346bf3a173e56bcb14b7b9\\u0026ptype=0\\u0026src=9\\u0026sl=1\\u0026order=1",
          "length": 0,
          "order": 1,
          "stream_type": 0,
          "p2p_type": 0*-/
        QString url = array.first().toObject().value("url").toString();
        if (func)
            func(url);
    });*/

    QString full_url = "https://api.live.bilibili.com/xlive/web-room/v2/index/getRoomPlayInfo?room_id=" + ac->roomId
            + "&no_playurl=0&mask=1&qn=10000&platform=web&protocol=0,1&format=0,1,2&codec=0,1&dolby=5&panorama=1";
    get(full_url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qWarning() << ("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject play_url = json.value("data").toObject().value("playurl_info").toObject().value("playurl").toObject();
        QJsonArray streams = play_url.value("stream").toArray();
        QString url;

        // 先抓取m3u8的视频
        foreach (auto _stream, streams)
        {
            auto stream = _stream.toObject();
            // m3u8:http_hls, flv:http_stream
            if (stream.value("protocol_name").toString() != "http_hls")
                continue;

            auto formats = stream.value("format").toArray(); // 这里有多个可播放的格式
            foreach (auto _format, formats)
            {
                auto format = _format.toObject();
                auto codecs = format.value("codec").toArray();
                foreach (auto _codec, codecs)
                {
                    auto codec = _codec.toObject();
                    QString codec_name = codec.value("codec_name").toString();
                    QString base_url = codec.value("base_url").toString();
                    auto url_infos = codec.value("url_info").toArray();
                    foreach (auto _url_info, url_infos)
                    {
                        auto url_info = _url_info.toObject();
                        QString host = url_info.value("host").toString();
                        QString extra = url_info.value("extra").toString();
                        qint64 stream_ttl = url_info.value("stream_ttl").toInt();

                        url = host + base_url;

                        if (!url.isEmpty())
                            break;
                    }
                    if (!url.isEmpty())
                        break;
                }
                if (!url.isEmpty())
                    break;
            }
            if (!url.isEmpty())
                break;
        }

        if (url.isEmpty())
        {
            qWarning() << "录播无法获取下载地址：" << full_url;
            return ;
        }
        if (url.endsWith("?"))
            url = url.left(url.length() - 1);
        if (func)
            func(url);
    });
}

/**
 * 触发进入直播间事件
 * 偶然看到的，未经测试
 */
void MainWindow::roomEntryAction()
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/roomEntryAction?room_id="
            + ac->roomId + "&platform=pc&csrf_token=" + ac->csrf_token + "&csrf=" + ac->csrf_token + "&visit_id=";
    post(url, QByteArray(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("进入直播间返回结果不为0：") << json.value("message").toString();
            return ;
        }
    });
}

void MainWindow::sendExpireGift()
{
    getBagList(24 * 3600 * 2); // 默认赠送两天内过期的
}

void MainWindow::getBagList(qint64 sendExpire)
{
    if (ac->roomId.isEmpty() || ac->browserCookie.isEmpty())
    {
        qWarning() << "房间为空，或未登录";
        return ;
    }
    /*{
        "code": 0,
        "message": "0",
        "ttl": 1,
        "data": {
            "list": [
                {
                    "bag_id": 232151929,
                    "gift_id": 30607,
                    "gift_name": "小心心",
                    "gift_num": 12,
                    "gift_type": 5,             // 0 = 普通, 6 = 礼盒, 2 = 加分卡
                    "expire_at": 1612972800,
                    "corner_mark": "4天",
                    "corner_color": "",
                    "count_map": [
                        {
                            "num": 1,
                            "text": ""
                        },
                        {
                            "num": 12,
                            "text": "全部"
                        }
                    ],
                    "bind_roomid": 0,
                    "bind_room_text": "",
                    "type": 1,
                    "card_image": "",
                    "card_gif": "",
                    "card_id": 0,
                    "card_record_id": 0,
                    "is_show_send": false
                },
                ......
    }*/

    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/gift/bag_list?t=1612663775421&room_id=" + ac->roomId;
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("获取包裹返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonArray bagArray = json.value("data").toObject().value("list").toArray();

        if (true)
        {
            foreach (QJsonValue val, bagArray)
            {
                QJsonObject bag = val.toObject();
                // 赠送礼物
                QString giftName = bag.value("gift_name").toString();
                int giftId = bag.value("gift_id").toInt();
                int giftNum = bag.value("gift_num").toInt();
                qint64 bagId = qint64(bag.value("bag_id").toDouble());
                QString cornerMark = bag.value("corner_mark").toString();
                // qInfo() << "当前礼物：" << giftName << "×" << giftNum << cornerMark << giftId <<bagId;
            }
        }

        if (sendExpire) // 赠送过期礼物
        {
            QList<int> whiteList{1, 6, 30607}; // 辣条、亿圆、小心心

            qint64 timestamp = QDateTime::currentSecsSinceEpoch();
            qint64 expireTs = timestamp + sendExpire; // 超过这个时间的都送
            foreach (QJsonValue val, bagArray)
            {
                QJsonObject bag = val.toObject();
                qint64 expire = qint64(bag.value("expire_at").toDouble());
                int giftId = bag.value("gift_id").toInt();
                if (!whiteList.contains(giftId) || expire == 0 || expire > expireTs)
                    continue ;

                // 赠送礼物
                QString giftName = bag.value("gift_name").toString();
                int giftNum = bag.value("gift_num").toInt();
                qint64 bagId = qint64(bag.value("bag_id").toDouble());
                QString cornerMark = bag.value("corner_mark").toString();
                qInfo() << "赠送过期礼物：" << giftId << giftName << "×" << giftNum << cornerMark << (expire-timestamp)/3600 << "小时";
                sendBagGift(giftId, giftNum, bagId);
            }
        }
    });
}

void MainWindow::updateExistGuards(int page)
{
    if (page == 0) // 表示是入口
    {
        if (updateGuarding)
            return ;

        page = 1;
        us->currentGuards.clear();
        guardInfos.clear();
        updateGuarding = true;

        // 参数是0的话，自动判断是否需要
        if (ac->browserCookie.isEmpty())
            return ;
    }

    const int pageSize = 29;

    auto judgeGuard = [=](QJsonObject user){
        /*{
            "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
            "guard_level": 3,
            "guard_sub_level": 0,
            "is_alive": 1,
            "medal_info": {
                "medal_color_border": 6809855,
                "medal_color_end": 5414290,
                "medal_color_start": 1725515,
                "medal_level": 23,
                "medal_name": "翊中人"
            },
            "rank": 71,
            "ruid": 5988102,
            "uid": 20285041,
            "username": "懒一夕智能科技"
        }*/
        QString username = user.value("username").toString();
        qint64 uid = static_cast<qint64>(user.value("uid").toDouble());
        int guardLevel = user.value("guard_level").toInt();
        guardInfos.append(LiveDanmaku(guardLevel, username, uid, QDateTime::currentDateTime()));
        us->currentGuards[uid] = username;

        if (uid == ac->cookieUid.toLongLong())
        {
            ac->cookieGuardLevel = guardLevel;
            if (ui->adjustDanmakuLongestCheck->isChecked())
                adjustDanmakuLongest();
        }

        int count = us->danmakuCounts->value("guard/" + snum(uid), 0).toInt();
        if (!count)
        {
            int count = 1;
            if (guardLevel == 3)
                count = 1;
            else if (guardLevel == 2)
                count = 10;
            else if (guardLevel == 1)
                count = 100;
            else
                qWarning() << "错误舰长等级：" << username << uid << guardLevel;
            us->danmakuCounts->setValue("guard/" + snum(uid), count);
            // qInfo() << "设置舰长：" << username << uid << count;
        }
    };

    QString _upUid = ac->upUid;
    QString url = "https://api.live.bilibili.com/xlive/app-room/v2/guardTab/topList?roomid="
            +ac->roomId+"&page="+snum(page)+"&ruid="+ac->upUid+"&page_size="+snum(pageSize);
    get(url, [=](QJsonObject json){
        if (_upUid != ac->upUid)
        {
            updateGuarding = false;
            return ;
        }

        QJsonObject data = json.value("data").toObject();

        if (page == 1)
        {
            QJsonArray top3 = data.value("top3").toArray();
            foreach (QJsonValue val, top3)
            {
                judgeGuard(val.toObject());
            }
        }

        QJsonArray list = data.value("list").toArray();
        foreach (QJsonValue val, list)
        {
            judgeGuard(val.toObject());
        }

        // 下一页
        QJsonObject info = data.value("info").toObject();
        int num = info.value("num").toInt();
        if (page * pageSize + 3 < num)
            updateExistGuards(page + 1);
        else // 全部结束了
        {
            if (ui->saveMonthGuardCheck->isChecked())
                saveMonthGuard();

            if (dailySettings)
                dailySettings->setValue("guard_count", us->currentGuards.size());
            updateGuarding = false;
            ui->guardCountLabel->setText(snum(us->currentGuards.size()));
        }
    });
}

/**
 * 有新上船后调用（不一定是第一次，可能是掉船了）
 * @param guardLevel 大航海等级
 */
void MainWindow::newGuardUpdate(const LiveDanmaku& danmaku)
{
    if (!hasEvent("NEW_GUARD_COUNT"))
        return ;
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByRoom?room_id=" + ac->roomId;
    get(url, [=](MyJson json) {
        int count = json.data().o("guard_info").i("count");
        LiveDanmaku ld = danmaku;
        ld.setNumber(count);
        triggerCmdEvent("NEW_GUARD_COUNT", ld.with(json.data()), true);
    });
}

/**
 * 获取高能榜上的用户（仅取第一页就行了）
 */
void MainWindow::updateOnlineGoldRank()
{
    /*{
        "code": 0,
        "message": "0",
        "ttl": 1,
        "data": {
            "onlineNum": 12,
            "OnlineRankItem": [
                {
                    "userRank": 1,
                    "uid": 8160635,
                    "name": "嘻嘻家の第二帅",
                    "face": "http://i2.hdslb.com/bfs/face/494fcc986807a944b79a027559d964c8b6b3addb.jpg",
                    "score": 3300,
                    "medalInfo": null,
                    "guard_level": 2
                },
                {
                    "userRank": 2,
                    "uid": 1274248,
                    "name": "贪睡的熊猫",
                    "face": "http://i1.hdslb.com/bfs/face/6241c9080e98a8988a3acc2df146236bad897be3.gif",
                    "score": 1782,
                    "medalInfo": {
                        "guardLevel": 3,
                        "medalColorStart": 1725515,
                        "medalColorEnd": 5414290,
                        "medalColorBorder": 6809855,
                        "medalName": "戒不掉",
                        "level": 21,
                        "targetId": 300702024,
                        "isLight": 1
                    },
                    "guard_level": 3
                },
                ...剩下10个...
            ],
            "ownInfo": {
                "uid": 20285041,
                "name": "懒一夕智能科技",
                "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
                "rank": -1,
                "needScore": 1,
                "score": 0,
                "guard_level": 0
            }
        }
    }*/
    QString _upUid = ac->upUid;
    QString url = "https://api.live.bilibili.com/xlive/general-interface/v1/rank/getOnlineGoldRank?roomId="
            +ac->roomId+"&page="+snum(1)+"&ruid="+ac->upUid+"&pageSize="+snum(50);
    onlineGoldRank.clear();
    get(url, [=](QJsonObject json){
        if (_upUid != ac->upUid)
            return ;

        QStringList names;
        QJsonObject data = json.value("data").toObject();
        QJsonArray array = data.value("OnlineRankItem").toArray();
        foreach (auto val, array)
        {
            QJsonObject item = val.toObject();
            qint64 uid = qint64(item.value("uid").toDouble());
            QString name = item.value("name").toString();
            int guard_level = item.value("guard_level").toInt(); // 没戴牌子也会算进去
            int score = item.value("score").toInt(); // 金瓜子数量
            int rank = item.value("userRank").toInt(); // 1,2,3...

            names.append(name + " " + snum(score));
            LiveDanmaku danmaku(name, uid, QDateTime(), false,
                                "", "", "");
            danmaku.setFirst(rank);
            danmaku.setTotalCoin(score);
            danmaku.extraJson = item;

            if (guard_level)
                danmaku.setGuardLevel(guard_level);

            if (!item.value("medalInfo").isNull())
            {
                QJsonObject medalInfo = item.value("medalInfo").toObject();

                QString anchorId = snum(qint64(medalInfo.value("targetId").toDouble()));
                if (medalInfo.contains("guardLevel") && anchorId == ac->roomId)
                    danmaku.setGuardLevel(medalInfo.value("guardLevel").toInt());

                qint64 medalColor = qint64(medalInfo.value("medalColorStart").toDouble());
                QString cs = QString::number(medalColor, 16);
                while (cs.size() < 6)
                    cs = "0" + cs;

                danmaku.setMedal(anchorId,
                                 medalInfo.value("medalName").toString(),
                                 medalInfo.value("level").toInt(),
                                 "",
                                 "");
            }

            onlineGoldRank.append(danmaku);
        }
        // qInfo() << "高能榜：" << names;
        updateOnlineRankGUI();
    });
}

void MainWindow::updateOnlineRankGUI()
{
    if (us->closeGui)
        return ;

    // 获取头像
    for (int i = 0; i < onlineGoldRank.size(); i++)
    {
        const LiveDanmaku &danmaku = onlineGoldRank.at(i);
        qint64 uid = danmaku.getUid();
        if (!pl->userHeaders.contains(uid))
        {
            QString url = danmaku.extraJson.value("face").toString();
            // qInfo() << "获取头像：" << uid << url;
            get(url, [=](QNetworkReply* reply){
                if (pl->userHeaders.contains(uid)) // 可能是多线程冲突了
                    return ;
                QByteArray jpegData = reply->readAll();
                QPixmap pixmap;
                pixmap.loadFromData(jpegData);
                if (pixmap.isNull())
                {
                    showError("获取头像图片出错");
                    qWarning() << "头像地址：" << url;
                    pixmap = QPixmap(":/icons/header1");
                }

                pl->userHeaders[uid] = pixmap;
                updateOnlineRankGUI();
            });
            return ;
        }
    }

    // 找到不同点，提升性能（前几应该都不变的）
    int diff = 0;
    for (diff = 0; diff < onlineGoldRank.size() && diff < ui->onlineRankListWidget->count(); diff++)
    {
        qint64 id1 = onlineGoldRank.at(diff).getUid();
        qint64 id2 = ui->onlineRankListWidget->item(diff)->data(Qt::UserRole).toLongLong();
        if (id1 != id2)
            break;
    }
    while (ui->onlineRankListWidget->count() > diff)
    {
        auto item = ui->onlineRankListWidget->item(diff);
        auto widget = ui->onlineRankListWidget->itemWidget(item);
        ui->onlineRankListWidget->takeItem(diff);
        delete item;
        widget->deleteLater();
    }

    // 放到GUI
    const int headerRadius = 24;
    int miniHeight = 0;
    for (int i = diff; i < onlineGoldRank.size(); i++)
    {
        auto& danmaku = onlineGoldRank.at(i);
        qint64 uid = danmaku.getUid();
        if (pl->userHeaders.contains(uid))
        {
            // 创建widget
            InteractiveButtonBase* card = new InteractiveButtonBase(ui->onlineRankListWidget);
            card->show();
            card->setRadius(rt->fluentRadius);
            // card->setCursor(Qt::PointingHandCursor);

            RoundedPixmapLabel* imgLabel = new RoundedPixmapLabel(card);
            imgLabel->setRadius(headerRadius);
            imgLabel->setFixedSize(headerRadius * 2, headerRadius * 2);
            imgLabel->show();
            imgLabel->setPixmap(pl->userHeaders.value(uid));

            QLabel* unameLabel = new QLabel(danmaku.getNickname(), card);
            unameLabel->show();
            // unameLabel->setFixedWidth(unameLabel->sizeHint().width());
            unameLabel->setAlignment(Qt::AlignHCenter);
            QLabel* scoreLabel = new QLabel(snum(danmaku.getTotalCoin()), card);
            scoreLabel->show();
            scoreLabel->setAlignment(Qt::AlignHCenter);
            unameLabel->setToolTip(danmaku.getNickname() + "  " + snum(danmaku.getTotalCoin()));

            QVBoxLayout* vlayout = new QVBoxLayout(card);
            vlayout->setAlignment(Qt::AlignHCenter);
            vlayout->addWidget(imgLabel);
            vlayout->addWidget(unameLabel);
            vlayout->addWidget(scoreLabel);
            vlayout->setSpacing(vlayout->spacing() / 2);
            // vlayout->setMargin(vlayout->margin() / 2);
            card->setLayout(vlayout);
            card->setFixedSize(imgLabel->width() + vlayout->margin() * 2,
                               imgLabel->height() + unameLabel->height() + scoreLabel->height() + vlayout->spacing()*2 + vlayout->margin() * 2);

            if (unameLabel->sizeHint().width() > imgLabel->width()) // 超出长度的
                unameLabel->setAlignment(Qt::AlignLeft);
            if (scoreLabel->sizeHint().width() > imgLabel->width())
                scoreLabel->setAlignment(Qt::AlignLeft);
            if (!miniHeight)
                miniHeight = card->height();

            // 创建item
            auto item = new QListWidgetItem();
            item->setData(Qt::UserRole, uid);
            ui->onlineRankListWidget->addItem(item);
            item->setSizeHint(card->size());
            ui->onlineRankListWidget->setItemWidget(item, card);

            // 点击事件
            connect(card, &InteractiveButtonBase::clicked, card, [=]{
                // TODO: 点击查看信息
            });
            connect(card, &InteractiveButtonBase::rightClicked, card, [=]{
                newFacileMenu;
                menu->addAction("打开首页", [=]{
                    QDesktopServices::openUrl(QUrl("https://space.bilibili.com/" + snum(uid)));
                });
                menu->exec();
            });
        }
    }
    if (miniHeight)
        ui->onlineRankListWidget->setFixedHeight(miniHeight);
    // ui->onlineRankDescLabel->setText(onlineGoldRank.size() ? "高\n能\n榜" : "");
    ui->onlineRankDescLabel->setPixmap(onlineGoldRank.size() ? QPixmap(":/icons/rank") : QPixmap());
    ui->onlineRankDescLabel->setMaximumSize(headerRadius * 4 / 3, headerRadius * 2);
}

/**
 * 添加本次直播的金瓜子礼物
 */
void MainWindow::appendLiveGift(const LiveDanmaku &danmaku)
{
    if (danmaku.getTotalCoin() == 0)
    {
        qWarning() << "添加礼物到liveGift错误：" << danmaku.toString();
        return ;
    }
    for (int i = 0; i < liveAllGifts.size(); i++)
    {
        auto his = liveAllGifts.at(i);
        if (his.getUid() == danmaku.getUid()
                && his.getGiftId() == danmaku.getGiftId())
        {
            liveAllGifts[i].addGift(danmaku.getNumber(), danmaku.getTotalCoin(), danmaku.getTimeline());
            return ;
        }
    }

    // 新建一个
    liveAllGifts.append(danmaku);
}

void MainWindow::appendLiveGuard(const LiveDanmaku &danmaku)
{
    if (!danmaku.is(MSG_GUARD_BUY))
    {
        qWarning() << "添加上船到liveGuard错误：" << danmaku.toString();
        return ;
    }
    for (int i = 0; i < liveAllGuards.size(); i++)
    {
        auto his = liveAllGuards.at(i);
        if (his.getUid() == danmaku.getUid()
                && his.getGiftId() == danmaku.getGiftId())
        {
            liveAllGuards[i].addGift(danmaku.getNumber(), danmaku.getTotalCoin(), danmaku.getTimeline());
            return ;
        }
    }

    // 新建一个
    liveAllGuards.append(danmaku);
}

void MainWindow::getPkMatchInfo()
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByRoom?room_id=" + pkRoomId;
    get(url, [=](MyJson json) {
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("获取PK匹配返回结果不为0：") << json.value("message").toString();
            return ;
        }

        MyJson data = json.data();
        LiveDanmaku danmaku;
        danmaku.with(data);

        // 计算高能榜综合
        qint64 sum = 0;
        data.o("online_gold_rank_info_v2").each("list", [&](MyJson usr){
            sum += usr.s("score").toLongLong(); // 这是String类型， 不是int
        });
        danmaku.setTotalCoin(sum); // 高能榜总积分
        danmaku.setNumber(data.o("online_gold_rank_info_v2").a("list").size()); // 高能榜总人数
        qInfo() << "对面高能榜积分总和：" << sum;
        // qDebug() << data.o("online_gold_rank_info_v2").a("list");
        triggerCmdEvent("PK_MATCH_INFO", danmaku, true);
    });
}

void MainWindow::getPkOnlineGuardPage(int page)
{
    static int guard1 = 0, guard2 = 0, guard3 = 0;
    if (page == 0)
    {
        page = 1;
        guard1 = guard2 = guard3 = 0;
    }

    auto addCount = [=](MyJson user) {
        int alive = user.i("is_alive");
        if (!alive)
            return ;
        QString username = user.s("username");
        int guard_level = user.i("guard_level");
        if (!alive)
            return ;
        if (guard_level == 1)
            guard1++;
        else if (guard_level == 2)
            guard2++;
        else
            guard3++;
    };

    QString url = "https://api.live.bilibili.com/xlive/app-room/v2/guardTab/topList?actionKey=appkey&appkey=27eb53fc9058f8c3&roomid=" + pkRoomId
            +"&page=" + QString::number(page) + "&ruid=" + pkUid + "&page_size=30";
    get(url, [=](MyJson json){
        MyJson data = json.data();
        // top3
        if (page == 1)
        {
            QJsonArray top3 = data.value("top3").toArray();
            foreach (QJsonValue val, top3)
                addCount(val.toObject());
        }

        // list
        QJsonArray list = data.value("list").toArray();
        foreach (QJsonValue val, list)
            addCount(val.toObject());

        // 下一页
        QJsonObject info = data.value("info").toObject();
        int page = info.value("page").toInt();
        int now = info.value("now").toInt();
        if (now < page)
            getPkOnlineGuardPage(now+1);
        else // 全部完成了
        {
            qInfo() << "舰长数量：" << guard1 << guard2 << guard3;
            LiveDanmaku danmaku;
            danmaku.setNumber(guard1 + guard2 + guard3);
            danmaku.extraJson.insert("guard1", guard1);
            danmaku.extraJson.insert("guard2", guard2);
            danmaku.extraJson.insert("guard3", guard3);
            triggerCmdEvent("PK_MATCH_ONLINE_GUARD", danmaku, true);
        }
    });
}

void MainWindow::setRoomDescription(QString roomDescription)
{
    ui->roomDescriptionBrowser->setText(roomDescription);
    QString text = ui->roomDescriptionBrowser->toPlainText();
    QTextCursor cursor = ui->roomDescriptionBrowser->textCursor();

    // 删除前面空白
    int count = 0;
    int len = text.length();
    while (count < len && QString("\n\r ").contains(QString(text.mid(count, 1))))
        count++;
    if (count)
    {
        cursor.setPosition(0);
        cursor.setPosition(count, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }

    // 删除后面空白
    text = ui->roomDescriptionBrowser->toPlainText();
    count = 0;
    len = text.length();
    while (count < len && QString("\n\r ").contains(QString(text.mid(len - count - 1, 1))))
        count++;
    if (count)
    {
        cursor.setPosition(len);
        cursor.setPosition(len - count, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }

    if (ui->roomDescriptionBrowser->toPlainText().isEmpty())
        ui->roomDescriptionBrowser->hide();
    else
        ui->roomDescriptionBrowser->show();
}

void MainWindow::upgradeWinningStreak(bool emitWinningStreak)
{
    QString url = "https://api.live.bilibili.com/av/v1/Battle/anchorBattleRank?uid=" + ac->upUid + "&room_id=" + ac->roomId + "&_=" + snum(QDateTime::currentMSecsSinceEpoch());
    // qInfo() << "winning streak:" << url;
    get(url, [=](MyJson json) {
        JO(json, data);
        JO(data, anchor_pk_info);
        JS(anchor_pk_info, pk_rank_name); // 段位名字：钻石传说
        JI(anchor_pk_info, pk_rank_star); // 段位级别：2
        JS(anchor_pk_info, first_rank_img_url); // 图片
        if (pk_rank_name.isEmpty())
            return ;
        if (ui->battleInfoWidget->isHidden())
            ui->battleInfoWidget->show();
        // if (!battleRankName.contains(pk_rank_name)) // 段位有变化

        JO(data, season_info);
        JI(season_info, current_season_id); // 赛季ID：38
        JS(season_info, current_season_name); // 赛季名称：PK大乱斗S12赛季
        this->currentSeasonId = current_season_id;
        ui->battleRankIconLabel->setToolTip(current_season_name + " (id:" + snum(current_season_id) + ")");

        JO(data, score_info);
        JI(score_info, total_win_num); // 总赢：309
        JI(score_info, max_win_num); // 最高连胜：16
        JI(score_info, total_num); // 总次数（>总赢）：454
        JI(score_info, total_tie_count); // 平局：3
        JI(score_info, total_lose_count); // 总失败数：142
        JI(score_info, win_count); // 当前连胜？：5
        JI(score_info, win_rate); // 胜率：69
        ui->winningStreakLabel->setText("连胜" + snum(win_count) + "场");
        QStringList nums {
            "总场次　：" + snum(total_num),
                    "胜　　场：" + snum(total_win_num),
                    "败　　场：" + snum(total_lose_count),
                    "最高连胜：" + snum(max_win_num),
                    "胜　　率：" + snum(win_rate) + "%"
        };
        ui->winningStreakLabel->setToolTip(nums.join("\n"));

        JA(data, assist_list); // 助攻列表

        JO(data, pk_url_info);
        this->pkRuleUrl = pk_url_info.s("pk_rule_url");

        JO(data, rank_info);
        JI(rank_info, rank); // 排名
        JL(rank_info, score); // 积分
        JL(anchor_pk_info, next_rank_need_score); // 下一级需要积分
        QStringList scores {
            "排名：" + snum(rank),
                    "积分：" + snum(score),
                    "升级需要积分：" + snum(next_rank_need_score)
        };
        ui->battleRankNameLabel->setToolTip(scores.join("\n"));

        JO(data, last_pk_info);
        JO(last_pk_info, match_info);
        JL(match_info, room_id); // 最后匹配的主播
        ac->lastMatchRoomId = room_id;

        if (emitWinningStreak && ac->winningStreak > 0 && ac->winningStreak == win_count - 1)
        {
            LiveDanmaku danmaku;
            danmaku.setNumber(win_count);
            danmaku.with(match_info);
            triggerCmdEvent("PK_WINNING_STREAK", danmaku);
        }
        ac->winningStreak = win_count;
    });
}

void MainWindow::getGiftList()
{
    get("https://api.live.bilibili.com/xlive/web-room/v1/giftPanel/giftConfig?platform=pc&room_id=" + ac->roomId, [=](MyJson json){
        if (json.code() != 0)
        {
            showError("获取直播间礼物", json.err());
            return ;
        }

        pl->allGiftMap.clear();
        auto list = json.data().a("list");
        for (QJsonValue val: list)
        {
            MyJson info = val.toObject();
            int id = info.i("id");
            int bag = info.i("bag_gift");
            if (!bag)
                continue;
            QString name = info.s("name");
            QString coinType = info.s("coin_type");
            int coin = info.i("price");
            QString desc = info.s("desc");

            LiveDanmaku gift("", id, name, 1, 0, QDateTime(), coinType, coin);
            gift.with(info);
            pl->allGiftMap[id] = gift;
        }
        // qInfo() << "直播间礼物数量：" << pl->allGiftMap.size();
    });
}

void MainWindow::getEmoticonList()
{
    get("https://api.live.bilibili.com/xlive/web-ucenter/v1/emoticon/GetEmoticonGuide",
        {"room_id", ac->roomId}, [=](MyJson json) {
        MyJson data = json.data();
        QJsonArray array = data.a("data");
        for (int i = 0; i < array.size(); i++)
        {
            MyJson emoObj = array.at(i).toObject();
            QJsonArray emotions = emoObj.a("emoticons");
            QString name = emoObj.s("pkg_name"); // 通用表情
            QString descript = emoObj.s("pkg_descript"); // "官方表情(系统)"  "房间专属表情"
            qint64 pkg_id = emoObj.l("pkg_id"); // 1  109028
            int pkg_type = emoObj.i("pkg_type"); // 1  2

            for (int j = 0; j < emotions.size(); j++)
            {
                MyJson o = emotions.at(j).toObject();
                Emoticon e;
                e.name = o.s("emoji"); // 赞
                e.description = o.s("descript"); // 可空
                e.id = o.l("emoticon_id"); // 147
                e.unique = o.s("emoticon_unique"); // official_147
                e.identity = o.i("identity");
                e.width = o.i("width");
                e.height = o.i("height");
                e.id_dynamic = o.i("id_dynamic");
                e.url = o.s("url");

                e.pkg_type = pkg_type;
                e.pkg_id = pkg_id;

                pl->emoticons.insert(e.unique, e);
            }
        }
    });
}

void MainWindow::on_autoSendWelcomeCheck_stateChanged(int arg1)
{
    us->setValue("danmaku/sendWelcome", ui->autoSendWelcomeCheck->isChecked());
    ui->sendWelcomeTextCheck->setEnabled(arg1);
    ui->sendWelcomeVoiceCheck->setEnabled(arg1);
}

void MainWindow::on_autoSendGiftCheck_stateChanged(int arg1)
{
    us->setValue("danmaku/sendGift", ui->autoSendGiftCheck->isChecked());
    ui->sendGiftTextCheck->setEnabled(arg1);
    ui->sendGiftVoiceCheck->setEnabled(arg1);
}

void MainWindow::on_autoWelcomeWordsEdit_textChanged()
{
    us->setValue("danmaku/autoWelcomeWords", ui->autoWelcomeWordsEdit->toPlainText());
}

void MainWindow::on_autoThankWordsEdit_textChanged()
{
    us->setValue("danmaku/autoThankWords", ui->autoThankWordsEdit->toPlainText());
}

void MainWindow::on_startLiveWordsEdit_editingFinished()
{
    us->setValue("live/startWords", ui->startLiveWordsEdit->text());
}

void MainWindow::on_endLiveWordsEdit_editingFinished()
{
    us->setValue("live/endWords", ui->endLiveWordsEdit->text());
}

void MainWindow::on_startLiveSendCheck_stateChanged(int arg1)
{
    us->setValue("live/startSend", ui->startLiveSendCheck->isChecked());
}

void MainWindow::on_autoSendAttentionCheck_stateChanged(int arg1)
{
    us->setValue("danmaku/sendAttention", ui->autoSendAttentionCheck->isChecked());
    ui->sendAttentionTextCheck->setEnabled(arg1);
    ui->sendAttentionVoiceCheck->setEnabled(arg1);
}

void MainWindow::on_autoAttentionWordsEdit_textChanged()
{
    us->setValue("danmaku/autoAttentionWords", ui->autoAttentionWordsEdit->toPlainText());
}

void MainWindow::on_sendWelcomeCDSpin_valueChanged(int arg1)
{
    us->setValue("danmaku/sendWelcomeCD", arg1);
}

void MainWindow::on_sendGiftCDSpin_valueChanged(int arg1)
{
    us->setValue("danmaku/sendGiftCD", arg1);
}

void MainWindow::on_sendAttentionCDSpin_valueChanged(int arg1)
{
    us->setValue("danmaku/sendAttentionCD", arg1);
}

void MainWindow::showDiangeHistory()
{
    QStringList list;
    int first = qMax(0, diangeHistory.size() - 10);
    for (int i = diangeHistory.size()-1; i >= first; i--)
    {
        Diange dg = diangeHistory.at(i);
        list << dg.name + "  -  " + dg.nickname + "  " + dg.time.toString("hh:mm:ss");
    }
    QString text = list.size() ? list.join("\n") : "没有点歌记录";
    QMessageBox::information(this, "点歌历史", text);
}

void MainWindow::appointAdmin(qint64 uid)
{
    if (ac->upUid != ac->cookieUid)
    {
        showError("房管操作", "仅主播可用");
        return ;
    }
    post("https://api.live.bilibili.com/xlive/web-ucenter/v1/roomAdmin/appoint",
         ("admin=" + snum(uid) + "&admin_level=1&csrf_token=" + ac->csrf_token + "&csrf=" + ac->csrf_token + "&visit_id=").toLatin1(),
         [=](MyJson json) {
        qInfo() << "任命房管：" << json;
        if (json.code() != 0)
        {
            showError("任命房管", json.msg());
            return ;
        }

    });
}

void MainWindow::dismissAdmin(qint64 uid)
{
    if (ac->upUid != ac->cookieUid)
    {
        showError("房管操作", "仅主播可用");
        return ;
    }
    post("https://api.live.bilibili.com/xlive/app-ucenter/v1/roomAdmin/dismiss",
         ("uid=" + snum(uid) + "&csrf_token=" + ac->csrf_token + "&csrf=" + ac->csrf_token + "&visit_id=").toLatin1(),
         [=](MyJson json) {
        qInfo() << "取消任命房管：" << json;
        if (json.code() != 0)
        {
            showError("取消任命房管", json.msg());
            return ;
        }
    });
}

void MainWindow::addBlockUser(qint64 uid, int hour)
{
    addBlockUser(uid, ac->roomId.toLongLong(), hour);
}

void MainWindow::addBlockUser(qint64 uid, qint64 roomId, int hour)
{
    if(ac->browserData.isEmpty())
    {
        showError("请先设置登录信息");
        return ;
    }

    if (localDebug)
    {
        localNotify("禁言用户 -> " + snum(uid) + " " + snum(hour) + " 小时");
        return ;
    }

    QString url = "https://api.live.bilibili.com/banned_service/v2/Silent/add_block_user";
    QString data = QString("roomid=%1&block_uid=%2&hour=%3&csrf_token=%4&csrd=%5&visit_id=")
                    .arg(roomId).arg(uid).arg(hour).arg(ac->csrf_token).arg(ac->csrf_token);
    qInfo() << "禁言：" << uid << hour;
    post(url, data.toStdString().data(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            showError("禁言失败", json.value("message").toString());
            return ;
        }
        QJsonObject d = json.value("data").toObject();
        qint64 id = static_cast<qint64>(d.value("id").toDouble());
        us->userBlockIds[uid] = id;
    });
}

void MainWindow::delBlockUser(qint64 uid)
{
    delBlockUser(uid, ac->roomId.toLongLong());
}

void MainWindow::delBlockUser(qint64 uid, qint64 roomId)
{
    if(ac->browserData.isEmpty())
    {
        showError("请先设置登录信息");
        return ;
    }

    if (localDebug)
    {
        localNotify("取消禁言 -> " + snum(uid));
        return ;
    }

    if (us->userBlockIds.contains(uid))
    {
        qInfo() << "取消禁言：" << uid << "  id =" << us->userBlockIds.value(uid);
        delRoomBlockUser(us->userBlockIds.value(uid));
        us->userBlockIds.remove(uid);
        return ;
    }

    // 获取直播间的网络ID，再取消屏蔽
    QString url = "https://api.live.bilibili.com/liveact/ajaxGetBlockList?roomid="+snum(roomId)+"&page=1";
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            if (code == 403)
                showError("取消禁言", "您没有权限");
            else
                showError("取消禁言", json.value("message").toString());
            return ;
        }
        QJsonArray list = json.value("data").toArray();
        foreach (QJsonValue val, list)
        {
            QJsonObject obj = val.toObject();
            if (static_cast<qint64>(obj.value("uid").toDouble()) == uid)
            {
                delRoomBlockUser(static_cast<qint64>(obj.value("id").toDouble())); // 获取房间ID
                break;
            }
        }

    });
}

void MainWindow::delRoomBlockUser(qint64 id)
{
    QString url = "https://api.live.bilibili.com/banned_service/v1/Silent/del_room_block_user";
    QString data = QString("id=%1&roomid=%2&csrf_token=%4&csrd=%5&visit_id=")
                    .arg(id).arg(ac->roomId).arg(ac->csrf_token).arg(ac->csrf_token);

    post(url, data.toStdString().data(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            showError("取消禁言", json.value("message").toString());
            return ;
        }

        // if (userBlockIds.values().contains(id))
        //    userBlockIds.remove(userBlockIds.key(id));
    });
}

void MainWindow::eternalBlockUser(qint64 uid, QString uname)
{
    if (us->eternalBlockUsers.contains(EternalBlockUser(uid, ac->roomId.toLongLong())))
    {
        localNotify("该用户已经在永久禁言中");
        return ;
    }

    addBlockUser(uid, 720);

    us->eternalBlockUsers.append(EternalBlockUser(uid, ac->roomId.toLongLong(), uname, ac->upName, ac->roomTitle, QDateTime::currentSecsSinceEpoch()));
    saveEternalBlockUsers();
    qInfo() << "添加永久禁言：" << uname << "    当前人数：" << us->eternalBlockUsers.size();
}

void MainWindow::cancelEternalBlockUser(qint64 uid)
{
    cancelEternalBlockUser(uid, ac->roomId.toLongLong());
}

void MainWindow::cancelEternalBlockUser(qint64 uid, qint64 roomId)
{
    EternalBlockUser user(uid, roomId);
    if (!us->eternalBlockUsers.contains(user))
        return ;

    us->eternalBlockUsers.removeOne(user);
    saveEternalBlockUsers();
    qInfo() << "移除永久禁言：" << uid << "    当前人数：" << us->eternalBlockUsers.size();
}

void MainWindow::cancelEternalBlockUserAndUnblock(qint64 uid)
{
    cancelEternalBlockUserAndUnblock(uid, ac->roomId.toLongLong());
}

void MainWindow::cancelEternalBlockUserAndUnblock(qint64 uid, qint64 roomId)
{
    cancelEternalBlockUser(uid, roomId);

    delBlockUser(uid, roomId);
}

void MainWindow::saveEternalBlockUsers()
{
    QJsonArray array;
    int size = us->eternalBlockUsers.size();
    for (int i = 0; i < size; i++)
        array.append(us->eternalBlockUsers.at(i).toJson());
    us->setValue("danmaku/eternalBlockUsers", array);
    qInfo() << "保存永久禁言，当前人数：" << us->eternalBlockUsers.size();
}

/**
 * 检测需要重新禁言的用户，越前面时间越早
 */
void MainWindow::detectEternalBlockUsers()
{
    qint64 currentSecond = QDateTime::currentSecsSinceEpoch();
    const int MAX_BLOCK_HOUR = 720;
    qint64 maxBlockSecond = MAX_BLOCK_HOUR * 3600;
    const int netDelay = 5; // 5秒的屏蔽时长
    bool blocked = false;
    for (int i = 0; i < us->eternalBlockUsers.size(); i++)
    {
        EternalBlockUser user = us->eternalBlockUsers.first();
        if (user.time + maxBlockSecond + netDelay >= currentSecond) // 仍在冷却中
            break;

        // 该补上禁言啦
        blocked = true;
        qInfo() << "永久禁言：重新禁言用户" << user.uid << user.uname << user.time << "->" << currentSecond;
        addBlockUser(user.uid, user.roomId, MAX_BLOCK_HOUR);

        user.time = currentSecond;
        us->eternalBlockUsers.removeFirst();
        us->eternalBlockUsers.append(user);
    }
    if (blocked)
        saveEternalBlockUsers();
}

void MainWindow::on_enableBlockCheck_clicked()
{
    bool enable = ui->enableBlockCheck->isChecked();
    us->setValue("block/enableBlock", enable);
    if (danmakuWindow)
        danmakuWindow->setEnableBlock(enable);

    if (enable)
        refreshBlockList();
}


void MainWindow::on_newbieTipCheck_clicked()
{
    bool enable = ui->newbieTipCheck->isChecked();
    us->setValue("block/newbieTip", enable);
    if (danmakuWindow)
        danmakuWindow->setNewbieTip(enable);
}

void MainWindow::on_autoBlockNewbieCheck_clicked()
{
    us->setValue("block/autoBlockNewbie", ui->autoBlockNewbieCheck->isChecked());
    ui->autoBlockNewbieNotifyCheck->setEnabled(ui->autoBlockNewbieCheck->isChecked());
}

void MainWindow::on_autoBlockNewbieKeysEdit_textChanged()
{
    us->setValue("block/autoBlockNewbieKeys", ui->autoBlockNewbieKeysEdit->toPlainText());
}

void MainWindow::on_autoBlockNewbieNotifyCheck_clicked()
{
    us->setValue("block/autoBlockNewbieNotify", ui->autoBlockNewbieNotifyCheck->isChecked());
}

void MainWindow::on_autoBlockNewbieNotifyWordsEdit_textChanged()
{
    us->setValue("block/autoBlockNewbieNotifyWords", ui->autoBlockNewbieNotifyWordsEdit->toPlainText());
}

void MainWindow::on_saveDanmakuToFileCheck_clicked()
{
    bool enabled = ui->saveDanmakuToFileCheck->isChecked();
    us->setValue("danmaku/saveDanmakuToFile", enabled);
    if (enabled)
        startSaveDanmakuToFile();
    else
        finishSaveDanmuToFile();
}

void MainWindow::on_promptBlockNewbieCheck_clicked()
{
    us->setValue("block/promptBlockNewbie", ui->promptBlockNewbieCheck->isChecked());
}

void MainWindow::on_promptBlockNewbieKeysEdit_textChanged()
{
    us->setValue("block/promptBlockNewbieKeys", ui->promptBlockNewbieKeysEdit->toPlainText());
}

void MainWindow::on_timerConnectServerCheck_clicked()
{
    bool enable = ui->timerConnectServerCheck->isChecked();
    us->setValue("live/timerConnectServer", enable);
    if (!isLiving() && enable)
        startConnectRoom();
    else if (!enable && (!socket || socket->state() == QAbstractSocket::UnconnectedState))
        startConnectRoom();
}

void MainWindow::on_startLiveHourSpin_valueChanged(int arg1)
{
    us->setValue("live/startLiveHour", ui->startLiveHourSpin->value());
    if (!justStart && ui->timerConnectServerCheck->isChecked() && connectServerTimer && !connectServerTimer->isActive())
        connectServerTimer->start();
}

void MainWindow::on_endLiveHourSpin_valueChanged(int arg1)
{
    us->setValue("live/endLiveHour", ui->endLiveHourSpin->value());
    if (!justStart && ui->timerConnectServerCheck->isChecked() && connectServerTimer && !connectServerTimer->isActive())
        connectServerTimer->start();
}

void MainWindow::on_calculateDailyDataCheck_clicked()
{
    bool enable = ui->calculateDailyDataCheck->isChecked();
    us->setValue("live/calculateDaliyData", enable);
    if (enable)
        startCalculateDailyData();
}

void MainWindow::on_pushButton_clicked()
{
    QString text = QDateTime::currentDateTime().toString("yyyy-MM-dd\n");
    text += "\n进来人次：" + snum(dailyCome);
    text += "\n观众人数：" + snum(us->userComeTimes.count());
    text += "\n弹幕数量：" + snum(dailyDanmaku);
    text += "\n新人弹幕：" + snum(dailyNewbieMsg);
    text += "\n新增关注：" + snum(dailyNewFans);
    text += "\n银瓜子数：" + snum(dailyGiftSilver);
    text += "\n金瓜子数：" + snum(dailyGiftGold);
    text += "\n上船次数：" + snum(dailyGuard);
    text += "\n最高人气：" + snum(dailyMaxPopul);
    text += "\n平均人气：" + snum(dailyAvePopul);

    text += "\n\n累计粉丝：" + snum(ac->currentFans);
    QMessageBox::information(this, "今日数据", text);
}

void MainWindow::on_removeDanmakuTipIntervalSpin_valueChanged(int arg1)
{
    this->removeDanmakuTipInterval = arg1 * 1000;
    us->setValue("danmaku/removeTipInterval", arg1);
}

void MainWindow::on_doveCheck_clicked()
{
    // 这里不做操作，重启失效
}

void MainWindow::on_notOnlyNewbieCheck_clicked()
{
    bool enable = ui->notOnlyNewbieCheck->isChecked();
    us->setValue("block/notOnlyNewbie", enable);
}

void MainWindow::on_pkAutoMelonCheck_clicked()
{
    bool enable = ui->pkAutoMelonCheck->isChecked();
    us->setValue("pk/autoMelon", enable);
}

void MainWindow::on_pkMaxGoldButton_clicked()
{
    bool ok = false;
    // 默认最大设置的是1000元，有钱人……
    int v = QInputDialog::getInt(this, "自动偷塔礼物上限", "单次PK偷塔赠送的金瓜子上限\n1元=1000金瓜子=100或10乱斗值（按B站规则会变）\n注意：每次偷塔、反偷塔时都单独判断上限，而非一次大乱斗的累加", pkMaxGold, 0, 1000000, 100, &ok);
    if (!ok)
        return ;

    us->setValue("pk/maxGold", pkMaxGold = v);
}

void MainWindow::on_pkJudgeEarlyButton_clicked()
{
    bool ok = false;
    int v = QInputDialog::getInt(this, "自动偷塔提前判断", "每次偷塔结束前n毫秒判断，根据网速酌情设置\n1秒=1000毫秒，下次大乱斗生效", pkJudgeEarly, 0, 5000, 500, &ok);
    if (!ok)
        return ;

    us->setValue("pk/judgeEarly", pkJudgeEarly = v);
}

template<typename T>
bool MainWindow::isConditionTrue(T a, T b, QString op) const
{
    if (op == "==" || op == "=")
        return a == b;
    if (op == "!=" || op == "<>")
        return a != b;
    if (op == ">")
        return a > b;
    if (op == ">=")
        return a >= b;
    if (op == "<")
        return a < b;
    if (op == "<=")
        return a <= b;
    qWarning() << "无法识别的比较模板类型：" << a << op << b;
    return false;
}

void MainWindow::on_roomIdEdit_returnPressed()
{
    if (socket->state() == QAbstractSocket::UnconnectedState)
    {
        startConnectRoom();
    }
}

void MainWindow::on_actionData_Path_triggered()
{
    QDesktopServices::openUrl(QUrl("file:///" + rt->dataPath, QUrl::TolerantMode));
}

/**
 * 显示实时弹幕
 */
void MainWindow::on_actionShow_Live_Danmaku_triggered()
{
    if (!danmakuWindow)
    {
        danmakuWindow = new LiveDanmakuWindow(this);

        connect(this, &MainWindow::signalNewDanmaku, danmakuWindow, [=](const LiveDanmaku &danmaku) {
            if (danmaku.is(MSG_DANMAKU))
            {
                if (isFilterRejected("FILTER_DANMAKU_MSG", danmaku))
                    return ;
            }
            else if (danmaku.is(MSG_WELCOME) || danmaku.is(MSG_WELCOME_GUARD))
            {
                if (isFilterRejected("FILTER_DANMAKU_COME", danmaku))
                    return ;
            }
            else if (danmaku.is(MSG_GIFT) || danmaku.is(MSG_GUARD_BUY))
            {
                if (isFilterRejected("FILTER_DANMAKU_GIFT", danmaku))
                    return ;
            }
            else if (danmaku.is(MSG_ATTENTION))
            {
                if (isFilterRejected("FILTER_DANMAKU_ATTENTION", danmaku))
                    return ;
            }

            danmakuWindow->slotNewLiveDanmaku(danmaku);
        });

        connect(this, SIGNAL(signalRemoveDanmaku(LiveDanmaku)), danmakuWindow, SLOT(slotOldLiveDanmakuRemoved(LiveDanmaku)));
        connect(danmakuWindow, SIGNAL(signalSendMsg(QString)), this, SLOT(sendMsg(QString)));
        connect(danmakuWindow, SIGNAL(signalAddBlockUser(qint64, int)), this, SLOT(addBlockUser(qint64, int)));
        connect(danmakuWindow, SIGNAL(signalDelBlockUser(qint64)), this, SLOT(delBlockUser(qint64)));
        connect(danmakuWindow, SIGNAL(signalEternalBlockUser(qint64,QString)), this, SLOT(eternalBlockUser(qint64,QString)));
        connect(danmakuWindow, SIGNAL(signalCancelEternalBlockUser(qint64)), this, SLOT(cancelEternalBlockUser(qint64)));
        connect(danmakuWindow, SIGNAL(signalAIReplyed(QString, qint64)), this, SLOT(slotAIReplyed(QString, qint64)));
        connect(danmakuWindow, SIGNAL(signalShowPkVideo()), this, SLOT(on_actionShow_PK_Video_triggered()));
        connect(danmakuWindow, &LiveDanmakuWindow::signalChangeWindowMode, this, [=]{
            danmakuWindow->deleteLater();
            danmakuWindow = nullptr;
            on_actionShow_Live_Danmaku_triggered(); // 重新加载
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalSendMsgToPk, this, [=](QString msg){
            if (!pking || pkRoomId.isEmpty())
                return ;
            qInfo() << "发送PK对面消息：" << pkRoomId << msg;
            sendRoomMsg(pkRoomId, msg);
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalMarkUser, this, [=](qint64 uid){
            if (judgeRobot)
                markNotRobot(uid);
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalTransMouse, this, [=](bool enabled){
            if (enabled)
                ui->closeTransMouseButton->show();
            else
                ui->closeTransMouseButton->hide();
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalAddCloudShieldKeyword, this, &MainWindow::addCloudShieldKeyword);
        connect(danmakuWindow, SIGNAL(signalAppointAdmin(qint64)), this, SLOT(appointAdmin(qint64)));
        connect(danmakuWindow, SIGNAL(signalDismissAdmin(qint64)), this, SLOT(dismissAdmin(qint64)));
        danmakuWindow->setEnableBlock(ui->enableBlockCheck->isChecked());
        danmakuWindow->setNewbieTip(ui->newbieTipCheck->isChecked());
        danmakuWindow->setIds(ac->upUid.toLongLong(), ac->roomId.toLongLong());
        danmakuWindow->setWindowIcon(this->windowIcon());
        danmakuWindow->setWindowTitle(this->windowTitle());
        danmakuWindow->hide();

        // PK中
        if (pkBattleType)
        {
            danmakuWindow->setPkStatus(1, pkRoomId.toLongLong(), pkUid.toLongLong(), pkUname);
        }

        // 添加弹幕
        QTimer::singleShot(0, [=]{
            danmakuWindow->removeAll();
            if (roomDanmakus.size())
            {
                for (int i = 0; i < roomDanmakus.size(); i++)
                    danmakuWindow->slotNewLiveDanmaku(roomDanmakus.at(i));
            }
            else // 没有之前的弹幕，从API重新pull下来
            {
                pullLiveDanmaku();
            }
            danmakuWindow->setAutoTranslate(ui->languageAutoTranslateCheck->isChecked());
            danmakuWindow->setAIReply(ui->AIReplyCheck->isChecked());

            if (pking)
            {
                danmakuWindow->setIds(ac->upUid.toLongLong(), ac->roomId.toLongLong());
            }
        });
    }

    bool hidding = danmakuWindow->isHidden();

    if (hidding)
    {
        danmakuWindow->show();
    }
    else
    {
        danmakuWindow->hide();
    }
    us->setValue("danmaku/liveWindow", hidding);
}

void MainWindow::on_actionSet_Cookie_triggered()
{
    bool ok = false;
    QString s = QInputDialog::getText(this, "设置Cookie", "设置用户登录的cookie", QLineEdit::Normal, ac->browserCookie, &ok);
    if (!ok)
        return ;

    if (!s.contains("SESSDATA="))
    {
        QMessageBox::warning(this, "设置Cookie", "设置Cookie失败，这不是Bilibili的浏览器Cookie");
        return ;
    }

    if (s.toLower().startsWith("cookie:"))
        s = s.replace(0, 7, "").trimmed();

    autoSetCookie(s);
}

void MainWindow::on_actionSet_Danmaku_Data_Format_triggered()
{
    bool ok = false;
    QString s = QInputDialog::getText(this, "设置Data", "设置弹幕的data\n自动从cookie中提取，可不用设置", QLineEdit::Normal, ac->browserData, &ok);
    if (!ok)
        return ;

    us->setValue("danmaku/browserData", ac->browserData = s);
    int posl = ac->browserData.indexOf("csrf_token=") + 9;
    int posr = ac->browserData.indexOf("&", posl);
    if (posr == -1) posr = ac->browserData.length();
    ac->csrf_token = ac->browserData.mid(posl, posr - posl);
}

void MainWindow::on_actionCookie_Help_triggered()
{
    QString steps = "发送弹幕前需按以下步骤注入登录信息：\n\n";
    steps += "步骤一：\n浏览器登录bilibili账号，按下F12（开发者调试工具）\n\n";
    steps += "步骤二：\n找到右边顶部的“Network”项，选择它下面的XHR\n\n";
    steps += "步骤三：\n刷新B站页面，中间多出一排列表，点其中任意一个，看右边“Headers”中的代码\n\n";
    steps += "步骤四：\n复制“Request Headers”下的“cookie”冒号后的一长串内容，粘贴到本程序“设置Cookie”中\n\n";
    steps += "设置好直播间ID、要发送的内容，即可发送弹幕！\n";
    steps += "注意：请勿过于频繁发送，容易被临时拉黑！";

    /*steps += "\n\n变量列表：\n";
    steps += "\\n：分成多条弹幕发送，间隔1.5秒";
    steps += "\n%hour%：根据时间替换为“早上”、“中午”、“晚上”等";
    steps += "\n%all_greet%：根据时间替换为“你好啊”、“早上好呀”、“晚饭吃了吗”、“还没睡呀”等";
    steps += "\n%greet%：根据时间替换为“你好”、“早上好”、“中午好”等";
    steps += "\n%tone%：随机替换为“啊”、“呀”";
    steps += "\n%punc%：随机替换为“~”、“！”";
    steps += "\n%tone/punc%：随机替换为“啊”、“呀”、“~”、“！”";*/
    QMessageBox::information(this, "定时弹幕", steps);
}

void MainWindow::on_actionCreate_Video_LRC_triggered()
{
    VideoLyricsCreator* vlc = new VideoLyricsCreator(us, nullptr);
    vlc->show();
}

void MainWindow::on_actionShow_Order_Player_Window_triggered()
{
    if (!musicWindow)
    {
        musicWindow = new OrderPlayerWindow(rt->dataPath, nullptr);
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongSucceed, this, [=](Song song, qint64 latency, int waiting){
            qInfo() << "点歌成功" << song.simpleString() << latency;

            // 获取UID
            qint64 uid = 0;
            for (int i = roomDanmakus.size()-1; i >= 0; i--)
            {
                const LiveDanmaku danmaku = roomDanmakus.at(i);
                if (!danmaku.is(MSG_DANMAKU))
                    continue;

                QString nick = danmaku.getNickname();
                if (nick == song.addBy)
                {
                    uid = danmaku.getUid();
                    break;
                }
            }

            LiveDanmaku danmaku(uid, song.addBy, song.name);
            danmaku.setPrevTimestamp(latency / 1000); // 毫秒转秒
            danmaku.setFirst(waiting); // 当前歌曲在第几首，例如1，则是下一首
            danmaku.with(song.toJson());

            triggerCmdEvent("ORDER_SONG_SUCCEED", danmaku);
            triggerCmdEvent("ORDER_SONG_SUCCEED_OVERRIDE", danmaku);
            if (hasEvent("ORDER_SONG_SUCCEED_OVERRIDE"))
                return ;

            if (latency < 180000) // 小于3分钟
            {
                QString tip = "成功点歌：【" + song.simpleString() + "】";
                localNotify(tip);
                if (ui->diangeReplyCheck->isChecked())
                {
                    if (waiting == 1 && latency > 20000)
                        sendNotifyMsg("成功点歌，下一首播放");
                    else if (waiting == 2 && latency > 20000)
                        sendNotifyMsg("成功点歌，下两首播放");
                    else // 多首队列
                        sendNotifyMsg("成功点歌");
                }
            }
            else // 超过3分钟
            {
                int minute = (latency+20000) / 60000;
                localNotify(snum(minute) + "分钟后播放【" + song.simpleString() + "】");
                if (ui->diangeReplyCheck->isChecked())
                    sendNotifyMsg("成功点歌，" + snum(minute) + "分钟后播放");
            }
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongPlayed, this, [=](Song song){
            localNotify("开始播放：" + song.simpleString());
            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setPrevTimestamp(song.addTime);
            danmaku.with(song.toJson());
            triggerCmdEvent("ORDER_SONG_PLAY", danmaku);
        });
        connect(musicWindow, &OrderPlayerWindow::signalCurrentSongChanged, this, [=](Song song){
            if (sendCurrentSongToSockets)
                sendJsonToSockets("CURRENT_SONG", song.toJson());

            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setPrevTimestamp(song.addTime);
            danmaku.with(song.toJson());
            triggerCmdEvent("CURRENT_SONG_CHANGED", danmaku);

            if (ui->playingSongToFileCheck->isChecked())
            {
                savePlayingSong();
            }
        });

        connect(musicWindow, &OrderPlayerWindow::signalSongPlayFinished, this, [=](Song song){
            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setPrevTimestamp(song.addTime);
            danmaku.with(song.toJson());
            triggerCmdEvent("SONG_PLAY_FINISHED", danmaku);
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongImproved, this, [=](Song song, int prev, int curr){
            localNotify("提升歌曲：" + song.name + " : " + snum(prev+1) + "->" + snum(curr));
            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setNumber(curr+1);
            danmaku.with(song.toJson());
            triggerCmdEvent("ORDER_SONG_IMPROVED", danmaku);
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongCutted, this, [=](Song song){
            // localNotify("已切歌");
            triggerCmdEvent("ORDER_SONG_CUTTED", LiveDanmaku(song.id, song.addBy, song.name).with(song.toJson()));
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongNotFound, this, [=](QString key){
            localNotify("未找到歌曲：" + key);
            triggerCmdEvent("ORDER_SONG_NOT_FOUND", LiveDanmaku(key));
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongModified, this, [=](const SongList& songs){
            if (ui->orderSongsToFileCheck->isChecked())
            {
                saveOrderSongs(songs);
            }
            if (sendSongListToSockets)
            {
                sendMusicList(songs);
            }
        });
        connect(musicWindow, &OrderPlayerWindow::signalLyricChanged, this, [=](){
            if (ui->songLyricsToFileCheck->isChecked())
            {
                saveSongLyrics();
            }
            if (sendLyricListToSockets)
            {
                sendLyricList();
            }
        });
        auto simulateMusicKey = [=]{
            if (!ui->autoPauseOuterMusicCheck->isChecked())
                return ;
#if defined (Q_OS_WIN)
            simulateKeys(ui->outerMusicKeyEdit->text());
#endif
        };
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongStarted, this, [=]{
            simulateMusicKey();
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongEnded, this, [=]{
            simulateMusicKey();
        });
        connect(musicWindow, &OrderPlayerWindow::signalWindowClosed, this, [=]{
            QTimer::singleShot(5000, this, [=]{ // 延迟5秒，避免程序关闭时先把点歌姬关了，但下次还是需要显示的
                us->setValue("danmaku/playerWindow", false);
            });
        });
    }

    bool hidding = musicWindow->isHidden();

    if (hidding)
    {
        musicWindow->show();
    }
    else
    {
        musicWindow->hide();
    }
    us->setValue("danmaku/playerWindow", hidding);
}

void MainWindow::on_diangeReplyCheck_clicked()
{
    us->setValue("danmaku/diangeReply", ui->diangeReplyCheck->isChecked());
}

void MainWindow::on_actionAbout_triggered()
{
    QString appVersion = GetFileVertion(QApplication::applicationFilePath()).trimmed();
    if (!appVersion.startsWith("v") && !appVersion.startsWith("V"))
        appVersion.insert(0, "v");

    QString text;
    text += QApplication::applicationName() + " " + appVersion;
    text += "\n\n本程序由心乂独立开发，参考多个开源项目实现。\n仅供个人学习、研究之用，禁止用于商业用途。\n\n";
    text += "QQ群：1038738410\n欢迎大家一起来交流反馈&功能研发&闲聊&搞事情&拯救地球\n\n";
    text += "GitHub: https://github.com/iwxyi";
    QMessageBox::information(this, "关于", text);
}

void MainWindow::on_actionGitHub_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/iwxyi/Bilibili-MagicalDanmaku"));
}

void MainWindow::on_actionCustom_Variant_triggered()
{
    QString text = saveCustomVariant();
    bool ok;
    text = TextInputDialog::getText(this, "自定义变量", "请输入自定义变量，可在答谢、定时中使用：\n示例格式：%var%=val", text, &ok);
    if (!ok)
        return ;
    us->setValue("danmaku/customVariant", text);

    restoreCustomVariant(text);
}

void MainWindow::on_actionVariant_Translation_triggered()
{
}

void MainWindow::on_actionSend_Long_Text_triggered()
{
    bool ok;
    QString text = QInputDialog::getText(this, "发送长文本", "请输入长文本（支持变量），分割为每次20字发送\n注意带有敏感词或特殊字符的部分将无法发送", QLineEdit::Normal, "", &ok);
    text = text.trimmed();
    if (!ok || text.isEmpty())
        return ;

    sendLongText(text);
}

void MainWindow::on_actionShow_Lucky_Draw_triggered()
{
    if (!luckyDrawWindow)
    {
        luckyDrawWindow = new LuckyDrawWindow(nullptr);
        connect(this, SIGNAL(signalNewDanmaku(const LiveDanmaku&)), luckyDrawWindow, SLOT(slotNewDanmaku(const LiveDanmaku&)));
    }
    luckyDrawWindow->show();
}

void MainWindow::on_actionGet_Play_Url_triggered()
{
    getRoomLiveVideoUrl([=](QString url) {
        QApplication::clipboard()->setText(url);
    });
}

void MainWindow::on_actionShow_Live_Video_triggered()
{
    if (ac->roomId.isEmpty())
        return ;
    if (!hasPermission())
    {
        on_actionBuy_VIP_triggered();
        return ;
    }

    LiveVideoPlayer* player = new LiveVideoPlayer(us, nullptr);
    connect(this, SIGNAL(signalLiveStart(QString)), player, SLOT(slotLiveStart(QString))); // 重新开播，需要刷新URL
    connect(player, SIGNAL(signalRestart()), this, SLOT(on_actionShow_Live_Video_triggered()));
    player->setAttribute(Qt::WA_DeleteOnClose, true);
    player->setRoomId(ac->roomId);
    player->setWindowTitle(ac->roomTitle + " - " + ac->upName);
    player->setWindowIcon(upFace);
    player->show();
}

void MainWindow::on_actionShow_PK_Video_triggered()
{
    if (pkRoomId.isEmpty())
        return ;

    LiveVideoPlayer* player = new LiveVideoPlayer(us, nullptr);
    player->setAttribute(Qt::WA_DeleteOnClose, true);
    player->setRoomId(pkRoomId);
    player->show();
}

void MainWindow::on_pkChuanmenCheck_clicked()
{
    pkChuanmenEnable = ui->pkChuanmenCheck->isChecked();
    us->setValue("pk/chuanmen", pkChuanmenEnable);

    if (pkChuanmenEnable)
    {
        connectPkRoom();
    }
}

void MainWindow::on_pkMsgSyncCheck_clicked()
{
    if (pkMsgSync == 0)
    {
        pkMsgSync = 1;
        ui->pkMsgSyncCheck->setCheckState(Qt::PartiallyChecked);
    }
    else if (pkMsgSync == 1)
    {
        pkMsgSync = 2;
        ui->pkMsgSyncCheck->setCheckState(Qt::Checked);
    }
    else if (pkMsgSync == 2)
    {
        pkMsgSync = 0;
        ui->pkMsgSyncCheck->setCheckState(Qt::Unchecked);
    }
    ui->pkMsgSyncCheck->setText(pkMsgSync == 1 ? "PK同步消息(仅视频)" : "PK同步消息");
    us->setValue("pk/msgSync", pkMsgSync);
}

void MainWindow::pkPre(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_PRE",
        "pk_status": 101,
        "pk_id": 100970480,
        "timestamp": 1607763991,
        "data": {
            "battle_type": 1, // 自己开始匹配？
            "uname": "SLe\\u4e36\\u82cf\\u4e50",
            "face": "http:\\/\\/i2.hdslb.com\\/bfs\\/face\\/4636d48aeefa1a177bc2bdfb595892d3648b80b1.jpg",
            "uid": 13330958,
            "room_id": 271270,
            "season_id": 30,
            "pre_timer": 10,
            "pk_votes_name": "\\u4e71\\u6597\\u503c",
            "end_win_task": null
        },
        "roomid": 22532956
    }*/

    /*{ 对面匹配过来的情况
        "cmd": "PK_BATTLE_PRE",
        "pk_status": 101,
        "pk_id": 100970387,
        "timestamp": 1607763565,
        "data": {
            "battle_type": 2, // 对面开始匹配？
            "uname": "\\u519c\\u6751\\u9493\\u9c7c\\u5c0f\\u6b66\\u5929\\u5929\\u76f4\\u64ad",
            "face": "http:\\/\\/i0.hdslb.com\\/bfs\\/face\\/fbaa9cfbc214164236cdbe79a77bcaae5334e9ef.jpg",
            "uid": 199775659, // 对面用户ID
            "room_id": 12298098, // 对面房间ID
            "season_id": 30,
            "pre_timer": 10,
            "pk_votes_name": "\\u4e71\\u6597\\u503c", // 乱斗值
            "end_win_task": null
        },
        "roomid": 22532956
    }*/

    // 大乱斗信息
    QJsonObject data = json.value("data").toObject();
    QString uname = data.value("uname").toString();
    QString uid = QString::number(static_cast<qint64>(data.value("uid").toDouble()));
    QString room_id = QString::number(static_cast<qint64>(data.value("room_id").toDouble()));
    pkUname = uname;
    pkRoomId = room_id;
    pkUid = uid;

    // 大乱斗类型
    pkBattleType = data.value("battle_type").toInt();
    if (pkBattleType == 1) // 普通大乱斗
        pkVideo = false;
    else if (pkBattleType == 2) // 视频大乱斗
        pkVideo = true;
    else
        pkVideo = false;

    qInfo() << "准备大乱斗，已匹配：" << static_cast<qint64>(json.value("pk_id").toDouble())  << "    类型：" << pkBattleType;
    if (danmakuWindow)
    {
        if (uname.isEmpty())
            danmakuWindow->setStatusText("大乱斗匹配中...");
        else if (!pkRoomId.isEmpty())
        {
            int pkCount = us->danmakuCounts->value("pk/" + pkRoomId, 0).toInt();
            QString text = "匹配：" + uname;
            if(pkCount > 0)
                text += "[" + QString::number(pkCount) + "]";
            danmakuWindow->setStatusText(text);
            qInfo() << "主播：" << uname << pkUid << pkRoomId;
            danmakuWindow->setPkStatus(1, pkRoomId.toLongLong(), pkUid.toLongLong(), pkUname);
        }
    }
    pkToLive = QDateTime::currentSecsSinceEpoch();

    if (pkChuanmenEnable /*&& battle_type == 2*/)
    {
        connectPkRoom();
    }
    ui->actionShow_PK_Video->setEnabled(true);
    ui->actionJoin_Battle->setEnabled(false);
    pkGifts.clear();

    // 处理PK对面直播间事件
    if (hasEvent("PK_MATCH_INFO"))
    {
        getPkMatchInfo();
    }
}

void MainWindow::pkStart(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_START",
        "data": {
            "battle_type": 1, // 不知道其他类型是啥
            "final_hit_votes": 0,
            "pk_end_time": 1605748342,
            "pk_frozen_time": 1605748332,
            "pk_start_time": 1605748032,
            "pk_votes_add": 0,
            "pk_votes_name": "乱斗值",
            "pk_votes_type": 0
        },
        "pk_id": 100729281,
        "pk_status": 201,
        "timestamp": 1605748032
    }*/

    QJsonObject data = json.value("data").toObject();
    pking = true;
    qint64 startTime = static_cast<qint64>(data.value("pk_start_time").toDouble());
    // qint64 endTime = static_cast<qint64>(data.value("pk_end_time").toDouble());
    pkEndTime = startTime + 300; // 因为endTime要延迟10秒，还是用startTime来判断吧
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 deltaEnd = pkEndTime - currentTime;
    QString roomId = ac->roomId;
    oppositeTouta = 0;
    cmAudience.clear();
    pkBattleType = data.value("battle_type").toInt();
    if (pkBattleType == 1) // 普通大乱斗
        pkVideo = false;
    else if (pkBattleType == 2) // 视频大乱斗
        pkVideo = true;
    else
        pkVideo = false;
    if (pkVideo)
    {
        pkToLive = currentTime;
    }

    // 结束前x秒
    pkEndingTimer->start(deltaEnd*1000 - pkJudgeEarly);

    pkTimer->start();
    if (danmakuWindow)
    {
        danmakuWindow->showStatusText();
        danmakuWindow->setToolTip(pkUname);
        danmakuWindow->setPkStatus(1, pkRoomId.toLongLong(), pkUid.toLongLong(), pkUname);
    }
    qint64 pkid = static_cast<qint64>(json.value("pk_id").toDouble());
    qInfo() << "开启大乱斗, id =" << pkid << "  room=" << pkRoomId << "  user=" << pkUid << "   battle_type=" << pkBattleType;

    // 保存PK信息
    int pkCount = us->danmakuCounts->value("pk/" + pkRoomId, 0).toInt();
    us->danmakuCounts->setValue("pk/" + pkRoomId, pkCount+1);
    qInfo() << "保存匹配次数：" << pkRoomId << pkUname << (pkCount+1);

    // PK提示
    QString text = "开启大乱斗：" + pkUname;
    if (pkCount)
        text += "  PK过" + QString::number(pkCount) + "次";
    else
        text += "  初次匹配";
    localNotify(text, pkUid.toLongLong());

    // 处理对面在线舰长界面
    if (hasEvent("PK_MATCH_ONLINE_GUARD"))
    {
        getPkOnlineGuardPage(0);
    }
}

void MainWindow::pkProcess(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_PROCESS",
        "data": {
            "battle_type": 1,
            "init_info": {
                "best_uname": "我今天超可爱0",
                "room_id": 22532956,
                "votes": 132
            },
            "match_info": {
                "best_uname": "银河的偶尔限定女友粉",
                "room_id": 21398069,
                "votes": 156
            }
        },
        "pk_id": 100729411,
        "pk_status": 201,
        "timestamp": 1605749908
    }*/
    QJsonObject data = json.value("data").toObject();
    int prevMyVotes = myVotes;
    int prevMatchVotes = matchVotes;
    if (snum(static_cast<qint64>(data.value("init_info").toObject().value("room_id").toDouble())) == ac->roomId)
    {
        myVotes = data.value("init_info").toObject().value("votes").toInt();
        matchVotes = data.value("match_info").toObject().value("votes").toInt();
    }
    else
    {
        myVotes = data.value("match_info").toObject().value("votes").toInt();
        matchVotes = data.value("init_info").toObject().value("votes").toInt();
    }

    if (!pkTimer->isActive())
        pkTimer->start();

    if (pkEnding)
    {
        qInfo() << "大乱斗进度(偷塔阶段)：" << myVotes << matchVotes << "   等待送到：" << pkVoting;

        // 显示偷塔情况
        if (prevMyVotes < myVotes)
        {
            int delta = myVotes - prevMyVotes;
            localNotify("[己方偷塔] + " + snum(delta));

            // B站返回的规则改了，偷塔的时候获取不到礼物了
            pkVoting -= delta;
            if (pkVoting < 0)
                pkVoting = 0;
        }
        if (prevMatchVotes < matchVotes)
        {
            oppositeTouta++;
            localNotify("[对方偷塔] + " + snum(matchVotes - prevMatchVotes));
            {
                // qInfo() << "pk偷塔信息：" << s;
                if (danmuLogStream)
                {
                    /* int melon = 100 / goldTransPk; // 单个吃瓜有多少乱斗值
                    int num = static_cast<int>((matchVotes-myVotes-pkVoting+melon)/melon);
                    QString s = QString("myVotes:%1, pkVoting:%2, matchVotes:%3, maxGold:%4, goldTransPk:%5, oppositeTouta:%6, need:%7")
                                .arg(myVotes).arg(pkVoting).arg(matchVotes).arg(getPkMaxGold(qMax(myVotes, matchVotes))).arg(goldTransPk).arg(oppositeTouta)
                                .arg(num);
                    (*danmuLogStream) << s << "\n";
                    (*danmuLogStream).flush(); // 立刻刷新到文件里 */
                }
            }
        }

        // 反偷塔，防止对方也在最后几秒刷礼物
        execTouta();
    }
    else
    {
        qInfo() << "大乱斗进度：" << myVotes << matchVotes;
    }
}

void MainWindow::pkEnd(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_END",
        "data": {
            "battle_type": 1,
            "init_info": {
                "best_uname": "我今天超可爱0",
                "room_id": 22532956,
                "votes": 10,
                "winner_type": 2
            },
            "match_info": {
                "best_uname": "",
                "room_id": 22195813,
                "votes": 0,
                "winner_type": -1
            },
            "timer": 10
        },
        "pk_id": "100729259",
        "pk_status": 401,
        "timestamp": 1605748006
    }*/
    // winner_type: 2赢，-1输，两边2平局

    QJsonObject data = json.value("data").toObject();
    if (pkVideo)
        pkToLive = QDateTime::currentSecsSinceEpoch();
    int winnerType1 = data.value("init_info").toObject().value("winner_type").toInt();
    int winnerType2 = data.value("match_info").toObject().value("winner_type").toInt();
    qint64 thisRoomId = static_cast<qint64>(data.value("init_info").toObject().value("room_id").toDouble());
    if (pkTimer)
        pkTimer->stop();
    if (pkEndingTimer)
        pkEndingTimer->stop();
    if (danmakuWindow)
    {
        danmakuWindow->hideStatusText();
        danmakuWindow->setToolTip("");
        danmakuWindow->setPkStatus(0, 0, 0, "");
    }
    QString bestName = "";
    int winnerType = 0;
    if (snum(thisRoomId) == ac->roomId) // init是自己
    {
        myVotes = data.value("init_info").toObject().value("votes").toInt();
        matchVotes = data.value("match_info").toObject().value("votes").toInt();
        bestName = data.value("init_info").toObject().value("best_uname").toString();
        winnerType = winnerType1;
    }
    else // match是自己
    {
        matchVotes = data.value("init_info").toObject().value("votes").toInt();
        myVotes = data.value("match_info").toObject().value("votes").toInt();
        bestName = data.value("match_info").toObject().value("best_uname").toString();
        winnerType = winnerType2;
    }

    bool ping = winnerType1 == winnerType2;
    bool result = winnerType > 0;

    qint64 bestUid = 0;
    int winCode = 0;
    if (!ping)
        winCode = winnerType;
    if (myVotes > 0)
    {
        for (int i = pkGifts.size()-1; i >= 0; i--)
            if (pkGifts.at(i).getNickname() == bestName)
            {
                bestUid = pkGifts.at(i).getUid();
                break;
            }
        LiveDanmaku danmaku(bestName, bestUid, winCode, myVotes);
        triggerCmdEvent("PK_BEST_UNAME", danmaku.with(data), true);
    }
    triggerCmdEvent("PK_END", LiveDanmaku(bestName, bestUid, winCode, myVotes).with(data), true);

    localNotify(QString("大乱斗 %1：%2 vs %3")
                                     .arg(ping ? "平局" : (result ? "胜利" : "失败"))
                                     .arg(myVotes)
                                     .arg(matchVotes));
    qInfo() << "大乱斗结束，结果：" << (ping ? "平局" : (result ? "胜利" : "失败")) << myVotes << matchVotes;
    myVotes = 0;
    matchVotes = 0;
    QTimer::singleShot(60000, [=]{
        if (pking) // 下一把PK，已经清空了
            return ;
        cmAudience.clear();
    });

    // 保存对面偷塔次数
    if (oppositeTouta && !pkUname.isEmpty())
    {
        int count = us->danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
        us->danmakuCounts->setValue("touta/" + pkRoomId, count+1);
    }

    // 清空大乱斗数据
    pking = false;
    pkEnding = false;
    pkVoting = 0;
    pkEndTime = 0;
    pkUname = "";
    pkUid = "";
    pkRoomId = "";
    myAudience.clear();
    oppositeAudience.clear();
    pkVideo = false;
    ui->actionShow_PK_Video->setEnabled(false);
    pkBattleType = 0;

    if (ac->cookieUid == ac->upUid)
        ui->actionJoin_Battle->setEnabled(true);

    if (pkSocket)
    {
        try {
            if (pkSocket->state() == QAbstractSocket::ConnectedState)
                pkSocket->close(); // 会自动deleterLater
            // pkSocket->deleteLater();
        } catch (...) {
            qCritical() << "delete pkSocket failed";
        }
        pkSocket = nullptr;
    }
}

void MainWindow::pkSettle(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_SETTLE_NEW",
        "pk_id": 200933662,
        "pk_status": 601,
        "timestamp": 1613959764,
        "data": {
            "pk_id": 200933662,
            "pk_status": 601,
            "settle_status": 1,
            "punish_end_time": 1613959944,
            "timestamp": 1613959764,
            "battle_type": 6,
            "init_info": {
                "room_id": 7259049,
                "result_type": -1,
                "votes": 0,
                "assist_info": []
            },
            "match_info": {
                "room_id": 21839758,
                "result_type": 2,
                "votes": 3,
                "assist_info": [
                    {
                        "rank": 1,
                        "uid": 412357310,
                        "face": "http:\\/\\/i0.hdslb.com\\/bfs\\/face\\/e97fbf0e412b936763033055821e1ff5df56565a.jpg",
                        "uname": "\\u6cab\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8"
                    }
                ]
            },
            "dm_conf": {
                "font_color": "#FFE10B",
                "bg_color": "#72C5E2"
            }
        }
    }*/

    QJsonObject data = json.value("data").toObject();
    if (pkVideo)
        pkToLive = QDateTime::currentSecsSinceEpoch();
    int winnerType1 = data.value("init_info").toObject().value("result_type").toInt();
    int winnerType2 = data.value("match_info").toObject().value("result_type").toInt();
    qint64 thisRoomId = static_cast<qint64>(data.value("init_info").toObject().value("room_id").toDouble());
    if (pkTimer)
        pkTimer->stop();
    if (pkEndingTimer)
        pkEndingTimer->stop();
    if (danmakuWindow)
    {
        danmakuWindow->hideStatusText();
        danmakuWindow->setToolTip("");
        danmakuWindow->setPkStatus(0, 0, 0, "");
    }
    QString bestName = "";
    int winCode = 0;
    if (snum(thisRoomId) == ac->roomId) // init是自己
    {
        myVotes = data.value("init_info").toObject().value("votes").toInt();
        matchVotes = data.value("match_info").toObject().value("votes").toInt();
        bestName = data.value("init_info").toObject().value("best_uname").toString();
        winCode = winnerType1;
    }
    else // match是自己
    {
        matchVotes = data.value("init_info").toObject().value("votes").toInt();
        myVotes = data.value("match_info").toObject().value("votes").toInt();
        bestName = data.value("match_info").toObject().value("best_uname").toString();
        winCode = winnerType2;
    }

    qint64 bestUid = 0;
    if (myVotes > 0)
    {
        for (int i = pkGifts.size()-1; i >= 0; i--)
            if (pkGifts.at(i).getNickname() == bestName)
            {
                bestUid = pkGifts.at(i).getUid();
                break;
            }
        LiveDanmaku danmaku(bestName, bestUid, winCode, myVotes);
        triggerCmdEvent("PK_BEST_UNAME", danmaku.with(data), true);
    }
    triggerCmdEvent("PK_END", LiveDanmaku(bestName, bestUid, winCode, myVotes).with(data), true);

    localNotify(QString("大乱斗 %1：%2 vs %3")
                                     .arg(winCode == 0 ? "平局" : (winCode > 0 ? "胜利" : "失败"))
                                     .arg(myVotes)
                                     .arg(matchVotes));
    qInfo() << "大乱斗结束，结果：" << (winCode == 0 ? "平局" : (winCode > 0 ? "胜利" : "失败")) << myVotes << matchVotes;
    myVotes = 0;
    matchVotes = 0;
    QTimer::singleShot(60000, [=]{
        if (pking) // 下一把PK，已经清空了
            return ;
        cmAudience.clear();
    });

    // 保存对面偷塔次数
    if (oppositeTouta && !pkUname.isEmpty())
    {
        int count = us->danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
        us->danmakuCounts->setValue("touta/" + pkRoomId, count+1);
    }

    // 清空大乱斗数据
    pking = false;
    pkEnding = false;
    pkVoting = 0;
    pkEndTime = 0;
    pkUname = "";
    pkUid = "";
    pkRoomId = "";
    myAudience.clear();
    oppositeAudience.clear();
    pkVideo = false;
    ui->actionShow_PK_Video->setEnabled(false);

    if (ac->cookieUid == ac->upUid)
        ui->actionJoin_Battle->setEnabled(true);

    if (pkSocket)
    {
        try {
            if (pkSocket->state() == QAbstractSocket::ConnectedState)
                pkSocket->close(); // 会自动deleterLater
            // pkSocket->deleteLater();
        } catch (...) {
            qCritical() << "delete pkSocket failed";
        }
        pkSocket = nullptr;
    }
}

void MainWindow::slotPkEndingTimeout()
{
    if (!pking) // 比如换房间了
    {
        qInfo() << "大乱斗结束前，逻辑不正确" << pking << ac->roomId
                 << QDateTime::currentSecsSinceEpoch() << pkEndTime;
        return ;
    }

    pkEndingTimer->stop();

    slotPkEnding();
}

int MainWindow::getPkMaxGold(int votes)
{
    if (!ui->pkAutoMaxGoldCheck->isChecked())
        return pkMaxGold;
    int money = qMax(0, votes / (1000 / goldTransPk) - pkMaxGold / goldTransPk); // 用于计算的价格
    double prop = pow(money, 1.0/3); // 自动调整的倍率，未限制
    double maxProp = 10.0 / qMax(1, pkMaxGold / 1000); // 限制最大是10倍；10元及以上则不使用倍率
    maxProp = qMax(1.0, maxProp);
    prop = qMin(qMax(1.0, prop), maxProp); // 限制倍率是 1 ~ 10 倍之间
    if (ui->pkAutoMelonCheck->isChecked() && debugPrint)
        qInfo() << ("[偷塔上限 " + snum(votes) + " => " + snum(int(pkMaxGold * prop)) + "金瓜子, "
                    +QString::number(pow(money, 1.0/3), 'f', 1)+"倍]");
    return int(pkMaxGold * prop);
}

bool MainWindow::execTouta()
{
    int maxGold = getPkMaxGold(qMax(myVotes, matchVotes));
    if (ui->pkAutoMelonCheck->isChecked()
            && myVotes + pkVoting <= matchVotes
            && myVotes + pkVoting + maxGold/goldTransPk > matchVotes
            /* && oppositeTouta < 6 // 对面之前未连续偷塔（允许被偷塔五次）（可能是连刷，这时候几个吃瓜偷塔没用） */
            && !toutaBlankList.contains(pkRoomId)
            && !magicalRooms.contains(pkRoomId))
    {
        // 调用送礼
        int giftId = 0, giftNum = 0;
        QString giftName;
        int giftUnit = 0, giftVote = 0; // 单价和单乱斗值
        int needVote = matchVotes - myVotes - pkVoting; // 至少需要多少积分才能不输给对面

        if (!ui->toutaGiftCheck->isChecked()) // 赠送吃瓜（比对面多1分）
        {
            giftId = 20004;
            giftUnit = 100;
            giftVote = giftUnit / goldTransPk; // 单个吃瓜有多少乱斗值
            giftNum = needVote / giftVote + 1; // 需要多少个礼物

            chiguaCount += giftNum;
        }
        else // 按类型赠送礼物
        {
            int minCost = 0x3f3f3f3f;
            foreach (auto gift, toutaGifts)
            {
                int unit = gift.getTotalCoin(); // 单价
                int vote = unit / goldTransPk; // 相当于多少乱斗值
                int num = needVote / vote + 1; // 起码要这个数量
                int cost = unit * num; // 总价
                if (cost > maxGold) // 总价超过上限
                    continue;

                if (toutaGiftCounts.size()) // 数量白名单
                {
                    int index = -1;
                    for (int i = 0; i < toutaGiftCounts.size(); i++)
                    {
                        int count = toutaGiftCounts.at(i);
                        if (count >= num) // 这个数量可以赢
                        {
                            int cos = unit * count;
                            if (cos > maxGold) // 总价超过上限
                                continue;

                            // 这个可以使用
                            index = i;
                            num = count;
                            cost = cos;
                            break;
                        }
                    }
                    if (index == -1) // 没有合适的数量
                        continue;
                }

                if (cost < minCost || (cost == minCost && num < giftNum)) // 以价格低、数量少的优先
                {
                    giftId = gift.getGiftId();
                    giftName = gift.getGiftName();
                    giftUnit = unit;
                    giftVote = vote;
                    giftNum = num;
                    minCost = cost;
                }
            }
        }

        if (!giftId)
        {
            qWarning() << "没有可赠送的合适礼物：" << needVote << "积分";
            return false;
        }

        sendGift(giftId, giftNum);
        localNotify(QString("[") + (oppositeTouta ? "反" : "") + "偷塔] " + snum(matchVotes-myVotes-pkVoting+1) + "，赠送 " + snum(giftNum) + " 个" + giftName);
        qInfo() << "大乱斗赠送" << giftNum << "个" << giftName << "：" << myVotes << "vs" << matchVotes;
        if (!localDebug)
            pkVoting += giftVote * giftNum; // 正在赠送中

        toutaCount++;
        toutaGold += giftUnit * giftNum;
        saveTouta();
        return true;
    }
    return false;
}

void MainWindow::getRoomCurrentAudiences(QString roomId, QSet<qint64> &audiences)
{
    QString url = "https://api.live.bilibili.com/ajax/msg";
    QStringList param{"roomid", roomId};
    connect(new NetUtil(url, param), &NetUtil::finished, this, [&](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "getRoomCurrentAudiences.ERROR:" << error.errorString();
            qCritical() << result;
            return ;
        }
        QJsonObject json = document.object();
        QJsonArray danmakus = json.value("data").toObject().value("room").toArray();
//        qInfo() << "初始化房间" << roomId << "观众：";
        for (int i = 0; i < danmakus.size(); i++)
        {
            LiveDanmaku danmaku = LiveDanmaku::fromDanmakuJson(danmakus.at(i).toObject());
//            if (!audiences.contains(danmaku.getUid()))
//                qInfo() << "    添加观众：" << danmaku.getNickname();
            audiences.insert(danmaku.getUid());
        }
    });
}

void MainWindow::connectPkRoom()
{
    if (pkRoomId.isEmpty())
        return ;
    qInfo() << "连接PK房间:" << pkRoomId;

    // 根据弹幕消息
    myAudience.clear();
    oppositeAudience.clear();

    getRoomCurrentAudiences(ac->roomId, myAudience);
    getRoomCurrentAudiences(pkRoomId, oppositeAudience);

    // 额外保存的许多本地弹幕消息
    for (int i = 0; i < roomDanmakus.size(); i++)
    {
        qint64 uid = roomDanmakus.at(i).getUid();
        if (uid)
            myAudience.insert(uid);
    }

    // 保存自己主播、对面主播（带头串门？？？）
    myAudience.insert(ac->upUid.toLongLong());
    oppositeAudience.insert(pkUid.toLongLong());

    // 连接socket
    if (pkSocket)
        pkSocket->deleteLater();
    pkSocket = new QWebSocket();

    connect(pkSocket, &QWebSocket::connected, this, [=]{
        SOCKET_DEB << "pkSocket connected";
        hostUseIndex--;
        // 5秒内发送认证包
        sendVeriPacket(pkSocket, pkRoomId, pkToken);
    });

    connect(pkSocket, &QWebSocket::disconnected, this, [=]{
        // 正在直播的时候突然断开了，比如掉线
        if (isLiving() && pkSocket)
        {
            if (pking && pkChuanmenEnable) // 需要继续连接
            {
                qWarning() << "pkSocket断开连接";

                // 判断是否要重新连接
                connectPkSocket();
            }
            else // 结束的情况，断开并清空
            {
                pkSocket->deleteLater();
                pkSocket = nullptr;
            }
            return ;
        }
    });

    connect(pkSocket, &QWebSocket::binaryMessageReceived, this, [=](const QByteArray &message){
        slotPkBinaryMessageReceived(message);
    });

    // ========== 开始连接 ==========
    connectPkSocket();
}

void MainWindow::connectPkSocket()
{
    if (pkRoomId.isEmpty())
        return ;

    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo";
    url += "?id="+pkRoomId+"&type=0";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray dataBa = reply->readAll();
        manager->deleteLater();
        delete request;
        reply->deleteLater();

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(dataBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "获取弹幕信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("pk对面直播间返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        pkToken = data.value("token").toString();
        QJsonArray hostArray = data.value("host_list").toArray();
        QString link = hostArray.first().toObject().value("host").toString();
        int port = hostArray.first().toObject().value("wss_port").toInt();
        QString host = QString("wss://%1:%2/sub").arg(link).arg(port);
        SOCKET_DEB << "pk.socket.host:" << host;

        // ========== 连接Socket ==========
        qInfo() << "连接 PK Socket：" << pkRoomId;
        if (!pkSocket)
            pkSocket = new QWebSocket();
        QSslConfiguration config = pkSocket->sslConfiguration();
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        config.setProtocol(QSsl::TlsV1SslV3);
        pkSocket->setSslConfiguration(config);
        pkSocket->open(host);
    });
    manager->get(*request);
}

void MainWindow::slotPkBinaryMessageReceived(const QByteArray &message)
{
    int operation = ((uchar)message[8] << 24)
            + ((uchar)message[9] << 16)
            + ((uchar)message[10] << 8)
            + ((uchar)message[11]);
    QByteArray body = message.right(message.length() - 16);
    SOCKET_INF << "pk操作码=" << operation << "  正文=" << (body.left(35)) << "...";

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(body, &error);
    QJsonObject json;
    if (error.error == QJsonParseError::NoError)
        json = document.object();

    if (operation == OP_SEND_MSG_REPLY) // 普通包
    {
        QString cmd;
        if (!json.isEmpty())
        {
            cmd = json.value("cmd").toString();
            qInfo() << "pk普通CMD：" << cmd;
            SOCKET_INF << json;
        }

        if (cmd == "NOTICE_MSG") // 全站广播（不用管）
        {
        }
        else if (cmd == "ROOM_RANK")
        {
        }
        else // 压缩弹幕消息
        {
            short protover = (message[6]<<8) + message[7];
            SOCKET_INF << "pk协议版本：" << protover;
            if (protover == 2) // 默认协议版本，zlib解压
            {
                uncompressPkBytes(body);
            }
        }
    }

//    delete[] body.data();
//    delete[] message.data();
    SOCKET_DEB << "PkSocket消息处理结束";
}

void MainWindow::trayAction(QSystemTrayIcon::ActivationReason reason)
{
    switch(reason)
    {
    case QSystemTrayIcon::Trigger:
        if (this->isHidden())
        {
            this->showNormal();
            this->activateWindow();
        }
        else
        {
            this->hide();
        }
        break;
    case QSystemTrayIcon::MiddleClick:
        on_actionShow_Live_Danmaku_triggered();
        break;
    case QSystemTrayIcon::Context:
    {
        newFacileMenu;
        menu->addAction(QIcon(":/icons/star"), "主界面", [=]{
            this->show();
            this->activateWindow();
        });
        menu->split()->addAction(QIcon(":/icons/danmu"), "弹幕姬", [=]{
            on_actionShow_Live_Danmaku_triggered();
        });
        menu->addAction(QIcon(":/icons/order_song"), "点歌姬", [=]{
            on_actionShow_Order_Player_Window_triggered();
        });
        menu->addAction(QIcon(":/icons/live"), "视频流", [=]{
            on_actionShow_Live_Video_triggered();
        });
        if (localDebug || debugPrint)
        {
            menu->split();
            menu->addAction(ui->actionLocal_Mode)->hide(!localDebug);
            menu->addAction(ui->actionDebug_Mode)->hide(!debugPrint);
        }

        menu->split();
        if (!hasPermission())
        {
            menu->addAction(QIcon(":/icons/crown"), "免费版", [=]{
                on_actionBuy_VIP_triggered();
            });
        }
        menu->addAction(QIcon(":/icons/cry"), "退出", [=]{
            menu->close();
            prepareQuit();
        })->text(ui->autoUpdateCheck->isChecked() && !rt->appNewVersion.isEmpty(), "中断更新并退出")
          ->text(isFileExist(QApplication::applicationDirPath() + "/update.zip"), "退出并更新");
        menu->exec();
    }
        break;
    default:
        break;
    }
}

void MainWindow::uncompressPkBytes(const QByteArray &body)
{
    QByteArray unc = zlibToQtUncompr(body.data(), body.size()+1);
    int offset = 0;
    short headerSize = 16;
    while (offset < unc.size() - headerSize)
    {
        int packSize = ((uchar)unc[offset+0] << 24)
                + ((uchar)unc[offset+1] << 16)
                + ((uchar)unc[offset+2] << 8)
                + (uchar)unc[offset+3];
        QByteArray jsonBa = unc.mid(offset + headerSize, packSize - headerSize);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(jsonBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << s8("pk解析解压后的JSON出错：") << error.errorString();
            qCritical() << s8("pk包数值：") << offset << packSize << "  解压后大小：" << unc.size();
            qCritical() << s8(">>pk当前JSON") << jsonBa;
            qCritical() << s8(">>pk解压正文") << unc;
            return ;
        }
        QJsonObject json = document.object();
        QString cmd = json.value("cmd").toString();
        SOCKET_INF << "pk解压后获取到CMD：" << cmd;
        if (cmd != "ROOM_BANNER" && cmd != "ACTIVITY_BANNER_UPDATE_V2" && cmd != "PANEL"
                && cmd != "ONLINERANK")
            SOCKET_INF << "pk单个JSON消息：" << offset << packSize << QString(jsonBa);
        handlePkMessage(json);

        offset += packSize;
    }
}

void MainWindow::handlePkMessage(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    qInfo() << s8(">pk消息命令：") << cmd;
    if (cmd == "DANMU_MSG" || cmd.startsWith("DANMU_MSG:")) // 收到弹幕
    {
        if (!pkMsgSync || (pkMsgSync == 1 && !pkVideo))
            return ;
        QJsonArray info = json.value("info").toArray();
        QJsonArray array = info[0].toArray();
        qint64 textColor = array[3].toInt(); // 弹幕颜色
        qint64 timestamp = static_cast<qint64>(array[4].toDouble()); // 13位
        QString msg = info[1].toString();
        QJsonArray user = info[2].toArray();
        qint64 uid = static_cast<qint64>(user[0].toDouble());
        QString username = user[1].toString();
        QString unameColor = user[7].toString();
        int level = info[4].toArray()[0].toInt();
        QJsonArray medal = info[3].toArray();
        int medal_level = 0;

        // 是否是串门的
        bool toView = pkBattleType &&
                ((!oppositeAudience.contains(uid) && myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() && medal.size() >= 4 &&
                     snum(static_cast<qint64>(medal[3].toDouble())) == ac->roomId));

        // !弹幕的时间戳是13位，其他的是10位！
        qInfo() << s8("pk接收到弹幕：") << username << msg << QDateTime::fromMSecsSinceEpoch(timestamp);

        // 添加到列表
        QString cs = QString::number(textColor, 16);
        while (cs.size() < 6)
            cs = "0" + cs;
        LiveDanmaku danmaku(username, msg, uid, level, QDateTime::fromMSecsSinceEpoch(timestamp),
                                                 unameColor, "#"+cs);
        if (medal.size() >= 4)
        {
            medal_level = medal[0].toInt();
            danmaku.setMedal(snum(static_cast<qint64>(medal[3].toDouble())),
                    medal[1].toString(), medal_level, medal[2].toString());
        }
        /*if (noReplyMsgs.contains(msg) && snum(uid) == cookieUid)
        {
            danmaku.setNoReply();
            noReplyMsgs.removeOne(msg);
        }
        else
            minuteDanmuPopular++;*/
        danmaku.setToView(toView);
        danmaku.setPkLink(true);
        appendNewLiveDanmaku(danmaku);

        triggerCmdEvent("PK_DANMU_MSG", danmaku.with(json));
    }
    else if (cmd == "SEND_GIFT") // 有人送礼
    {
        if (!pkMsgSync || (pkMsgSync == 1 && !pkVideo))
            return ;
        QJsonObject data = json.value("data").toObject();
        int giftId = data.value("giftId").toInt();
        int giftType = data.value("giftType").toInt(); // 不知道是啥，金瓜子1，银瓜子（小心心、辣条）5？
        QString giftName = data.value("giftName").toString();
        QString username = data.value("uname").toString();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        int num = data.value("num").toInt();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble()); // 秒
        timestamp = QDateTime::currentSecsSinceEpoch(); // *不管送出礼物的时间，只管机器人接收到的时间
        QString coinType = data.value("coin_type").toString();
        int totalCoin = data.value("total_coin").toInt();

        qInfo() << s8("接收到送礼：") << username << giftId << giftName << num << s8("  总价值：") << totalCoin << coinType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        QString anchorRoomId;
        if (!data.value("medal_info").isNull())
        {
            QJsonObject medalInfo = data.value("medal_info").toObject();
            anchorRoomId = snum(qint64(medalInfo.value("anchor_room_id").toDouble())); // !注意：这个一直为0！
            QString anchorUname = medalInfo.value("anchor_uname").toString(); // !注意：也是空的
            int guardLevel = medalInfo.value("guard_level").toInt();
            int isLighted = medalInfo.value("is_lighted").toInt();
            int medalColor = medalInfo.value("medal_color").toInt();
            int medalColorBorder = medalInfo.value("medal_color_border").toInt();
            int medalColorEnd = medalInfo.value("medal_color_end").toInt();
            int medalColorStart = medalInfo.value("medal_color_start").toInt();
            int medalLevel = medalInfo.value("medal_level").toInt();
            QString medalName = medalInfo.value("medal_name").toString();
            QString spacial = medalInfo.value("special").toString();
            QString targetId = snum(qint64(medalInfo.value("target_id").toDouble())); // 目标用户ID
            if (!medalName.isEmpty())
            {
                QString cs = QString::number(medalColor, 16);
                while (cs.size() < 6)
                    cs = "0" + cs;
                danmaku.setMedal(anchorRoomId, medalName, medalLevel, cs, anchorUname);
            }
        }

        // 是否是串门的
        bool toView = pkBattleType &&
                ((!oppositeAudience.contains(uid) && myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() && anchorRoomId == ac->roomId));

        danmaku.setPkLink(true);

        if (toView) // 自己这边过去送礼物，居心何在！
            appendNewLiveDanmaku(danmaku);

        triggerCmdEvent("PK_" + cmd, danmaku.with(data));
    }
    else if (cmd == "INTERACT_WORD")
    {
        if (!pkChuanmenEnable) // 可能是中途关了
            return ;
        QJsonObject data = json.value("data").toObject();
        int msgType = data.value("msg_type").toInt(); // 1进入直播间，2关注，3分享直播间，4特别关注
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        bool isadmin = data.value("isadmin").toBool();
        QString unameColor = data.value("uname_color").toString();
        bool isSpread = data.value("is_spread").toBool();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        QJsonObject fansMedal = data.value("fans_medal").toObject();
        QString roomId = snum(qint64(data.value("room_id").toDouble()));
        bool toView = pkBattleType &&
                ((!oppositeAudience.contains(uid) && myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() &&
                     snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())) == ac->roomId));
        bool attentionToMyRoom = false;
        if (!toView) // 不是自己方过去串门的
        {
            if (roomId == ac->roomId && msgType == 2) // 在对面关注当前主播
                attentionToMyRoom = true;
            else
                if (!pkMsgSync || (pkMsgSync == 1 && !pkVideo))
                    return ;
        }
        if (toView || attentionToMyRoom)
        {
            if (!cmAudience.contains(uid))
                cmAudience.insert(uid, timestamp);
        }

        // qInfo() << s8("pk观众互动：") << username << spreadDesc;
        QString localName = getLocalNickname(uid);
        LiveDanmaku danmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp), isadmin,
                            unameColor, spreadDesc, spreadInfo);
        danmaku.setMedal(snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())),
                         fansMedal.value("medal_name").toString(),
                         fansMedal.value("medal_level").toInt(),
                         QString("#%1").arg(fansMedal.value("medal_color").toInt(), 6, 16, QLatin1Char('0')),
                         "");
        danmaku.setToView(toView);
        danmaku.setPkLink(true);

        if (attentionToMyRoom)
        {
            danmaku.transToAttention(timestamp);
            localNotify("对面的 " + username + " 关注了本直播间", uid);
            triggerCmdEvent("ATTENTION_ON_OPPOSITE", danmaku.with(data));
        }
        else if (msgType == 1)
        {
            if (toView)
            {
                localNotify(username + " 跑去对面串门", uid); // 显示一个短通知，就不作为一个弹幕了
                triggerCmdEvent("CALL_ON_OPPOSITE", danmaku.with(data));
            }
            triggerCmdEvent("PK_" + cmd, danmaku.with(data));
        }
        else if (msgType == 2)
        {
            danmaku.transToAttention(timestamp);
            if (toView)
            {
                localNotify(username + " 关注了对面直播间", uid); // XXX
                triggerCmdEvent("ATTENTION_OPPOSITE", danmaku.with(data));
            }
            triggerCmdEvent("PK_ATTENTION", danmaku.with(data));
        }
        else if (msgType == 3)
        {
            danmaku.transToShare();
            if (toView)
            {
                localNotify(username + " 分享了对面直播间", uid); // XXX
                triggerCmdEvent("SHARE_OPPOSITE", danmaku.with(data));
            }
            triggerCmdEvent("PK_SHARE", danmaku.with(data));
        }
        // appendNewLiveDanmaku(danmaku);
    }
    else if (cmd == "CUT_OFF")
    {
        localNotify("对面直播间被超管切断");
    }
}

bool MainWindow::shallAutoMsg() const
{
    return !ui->sendAutoOnlyLiveCheck->isChecked() || (isLiving() /*&& popularVal > 1*/);
}

bool MainWindow::shallAutoMsg(const QString &sl) const
{
    if (sl.contains("%living%"))
        return true;
    return shallAutoMsg();
}

bool MainWindow::shallAutoMsg(const QString &sl, bool &manual)
{
    if (sl.contains("%living%"))
    {
        manual = true;
        return true;
    }
    return shallAutoMsg();
}

bool MainWindow::shallSpeakText()
{
    bool rst = !ui->dontSpeakOnPlayingSongCheck->isChecked() || !musicWindow || !musicWindow->isPlaying();
    if (!rst && debugPrint)
        localNotify("[放歌中，跳过语音]");
    return rst;
}

void MainWindow::addBannedWord(QString word, QString anchor)
{
    if (word.isEmpty())
        return ;

    QString text = ui->autoBlockNewbieKeysEdit->toPlainText();
    if (anchor.isEmpty())
    {
        if (anchor.endsWith("|") || word.startsWith("|"))
            text += word;
        else
            text += "|" + word;
    }
    else
    {
        if (!anchor.startsWith("|"))
            anchor = "|" + anchor;
        text.replace(anchor, "|" + word + anchor);
    }
    ui->autoBlockNewbieKeysEdit->setPlainText(text);

    text = ui->promptBlockNewbieKeysEdit->toPlainText();

    if (anchor.isEmpty())
    {
        if (anchor.endsWith("|") || word.startsWith("|"))
            text += word;
        else
            text += "|" + word;
    }
    else
    {
        if (!anchor.startsWith("|"))
            anchor = "|" + anchor;
        text.replace(anchor, "|" + word + anchor);
    }
    ui->promptBlockNewbieKeysEdit->setPlainText(text);
}

void MainWindow::saveMonthGuard()
{
    QDir dir(rt->dataPath + "guard_month");
    dir.mkpath(dir.absolutePath());
    QDate date = QDate::currentDate();
    QString fileName = QString("%1_%2-%3.csv").arg(ac->roomId).arg(date.year()).arg(date.month());
    QString filePath = dir.absoluteFilePath(dir.absoluteFilePath(fileName));

    QFile file(filePath);
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    stream << QString("UID,昵称,级别,备注\n").toUtf8();
    auto getGuardName = [=](int level) {
        if (level == 1)
            return QString("总督").toUtf8();
        else if (level == 2)
            return QString("提督").toUtf8();
        return QString("舰长").toUtf8();
    };
    for (int i = 0; i < guardInfos.size(); i++)
    {
        LiveDanmaku danmaku = guardInfos.at(i);
        stream << danmaku.getUid() << ","
               << danmaku.getNickname() << ","
               << getGuardName(danmaku.getGuard()) << "\n";
    }

    file.close();
}

void MainWindow::saveEveryGuard(LiveDanmaku danmaku)
{
    QDir dir(rt->dataPath + "guard_histories");
    dir.mkpath(dir.absolutePath());
    QString filePath = dir.absoluteFilePath(ac->roomId + ".csv");

    QFile file(filePath);
    bool exists = file.exists();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        qWarning() << "打开上船记录文件失败：" << filePath;
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    if (!exists)
        stream << QString("日期,时间,昵称,礼物,数量,累计,UID,备注\n").toUtf8();

    stream << danmaku.getTimeline().toString("yyyy-MM-dd") << ","
           << danmaku.getTimeline().toString("hh:mm") << ","
           << danmaku.getNickname() << ","
           << danmaku.getGiftName() << ","
           << danmaku.getNumber() << ","
           << us->danmakuCounts->value("guard/" + snum(danmaku.getUid()), 0).toInt() << ","
           << danmaku.getUid() << ","
           << us->userMarks->value("base/" + snum(danmaku.getUid()), "").toString() << "\n";

    file.close();
}

void MainWindow::saveEveryGift(LiveDanmaku danmaku)
{
    QDir dir(rt->dataPath + "gift_histories");
    dir.mkpath(dir.absolutePath());
    QDate date = QDate::currentDate();
    QString fileName = QString("%1_%2-%3.csv").arg(ac->roomId).arg(date.year()).arg(date.month());
    QString filePath = dir.absoluteFilePath(fileName);

    QFile file(filePath);
    bool exists = file.exists();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        qWarning() << "打开礼物记录文件失败：" << filePath;
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    if (!exists)
        stream << QString("日期,时间,昵称,礼物,数量,金瓜子,UID\n").toUtf8();

    stream << danmaku.getTimeline().toString("yyyy-MM-dd") << ","
           << danmaku.getTimeline().toString("hh:mm") << ","
           << danmaku.getNickname() << ","
           << danmaku.getGiftName() << ","
           << danmaku.getNumber() << ","
           << (danmaku.isGoldCoin() ? danmaku.getTotalCoin() : 0) << ","
           << danmaku.getUid() << "\n";

    file.close();
}

void MainWindow::appendFileLine(QString filePath, QString format, LiveDanmaku danmaku)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        qWarning() << "打开文件失败：" << filePath;
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!codeFileCodec.isEmpty())
        stream.setCodec(codeFileCodec.toUtf8());
    stream << processDanmakuVariants(format, danmaku) << "\n";
    file.close();
}

void MainWindow::releaseLiveData(bool prepare)
{
    ui->guardCountLabel->setText("0");
    ui->fansCountLabel->setText("0");
    ui->fansClubCountLabel->setText("0");
    ui->roomRankLabel->setText("0");
    ui->roomRankLabel->setToolTip("");
    ui->roomRankTextLabel->setText("热门榜");
    ui->popularityLabel->setText("0");
    ui->danmuCountLabel->setText("0");

    if (!prepare) // 切换房间或者断开连接
    {
        pking = false;
        pkUid = "";
        pkUname = "";
        pkRoomId = "";
        pkEnding = false;
        pkVoting = 0;
        pkVideo = false;
        pkTimer->stop();
        pkEndTime = 0;
        pkBattleType = 0;
        pkEndingTimer->stop();
        myAudience.clear();
        oppositeAudience.clear();
        fansList.clear();
        us->currentGuards.clear();
        guardInfos.clear();
        onlineGoldRank.clear();
        ac->currentFans = 0;
        ac->currentFansClub = 0;

        autoMsgQueues.clear();
        for (int i = 0; i < CHANNEL_COUNT; i++)
            msgCds[i] = 0;
        for (int i = 0; i < CHANNEL_COUNT; i++)
            msgWaits[i] = WAIT_CHANNEL_MAX;
        roomDanmakus.clear();
        pkGifts.clear();

        finishSaveDanmuToFile();

        if (ui->saveRecvCmdsCheck->isChecked())
        {
            ui->saveRecvCmdsCheck->setChecked(false);
            on_saveRecvCmdsCheck_clicked();
        }

        if (pushCmdsFile)
        {
            on_pushRecvCmdsButton_clicked();
        }

        ac->cookieGuardLevel = 0;
        if (ui->adjustDanmakuLongestCheck->isChecked())
            adjustDanmakuLongest();

        for (int i = 0; i < ui->giftListWidget->count(); i++)
        {
            auto item = ui->giftListWidget->item(i);
            auto widget = ui->giftListWidget->itemWidget(item);
            widget->deleteLater();
        }
        ui->giftListWidget->clear();
        ui->onlineRankListWidget->clear();
        pl->allGiftMap.clear();
    }
    else // 下播，依旧保持连接
    {
    }

    danmuPopulQueue.clear();
    minuteDanmuPopul = 0;
    danmuPopulValue = 0;

    diangeHistory.clear();
    ui->diangeHistoryListWidget->clear();

    statusLabel->setText("");
    this->popularVal = 0;

    liveTimestamp = QDateTime::currentMSecsSinceEpoch();
    xliveHeartBeatTimer->stop();

    // 本次直播数据
    liveAllGifts.clear();
    liveAllGuards.clear();

    if (danmakuWindow)
    {
        danmakuWindow->hideStatusText();
        danmakuWindow->setIds(0, 0);
        danmakuWindow->releaseLiveData(prepare);
    }

    if (pkSocket)
    {
        if (pkSocket->state() == QAbstractSocket::ConnectedState)
            pkSocket->close();
        pkSocket = nullptr;
    }

    // 结束游戏
    if (liveOpenService->isPlaying())
    {
        qInfo() << "退出前结束互动玩法...";
        QEventLoop loop;
        connect(liveOpenService, SIGNAL(signalEnd(bool)), &loop, SLOT(quit()));
        liveOpenService->end();
        loop.exec();
    }

    ui->actionShow_Live_Video->setEnabled(false);
    ui->actionShow_PK_Video->setEnabled(false);
    ui->actionJoin_Battle->setEnabled(false);

    finishLiveRecord();
    saveCalculateDailyData();

    QPixmap face = ac->roomId.isEmpty() ? QPixmap() : toCirclePixmap(upFace);
    setWindowIcon(face);
    tray->setIcon(face);

    // 清理一周没来的用户
    int day = ui->autoClearComeIntervalSpin->value();
    us->danmakuCounts->beginGroup("comeTime");
    QStringList removedKeys;
    auto keys = us->danmakuCounts->allKeys();
    qint64 week = QDateTime::currentSecsSinceEpoch() - day * 24 * 3600;
    foreach (auto key, keys)
    {
        qint64 value = us->danmakuCounts->value(key).toLongLong();
        if (value < week)
        {
            removedKeys.append(key);
        }
    }
    us->danmakuCounts->endGroup();

    us->danmakuCounts->beginGroup("comeTime");
    foreach (auto key, removedKeys)
    {
        us->danmakuCounts->remove(key);
    }
    us->danmakuCounts->endGroup();

    us->danmakuCounts->beginGroup("come");
    foreach (auto key, removedKeys)
    {
        us->danmakuCounts->remove(key);
    }
    us->danmakuCounts->endGroup();
    us->danmakuCounts->sync();
}

QRect MainWindow::getScreenRect()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenRect = screen->availableVirtualGeometry();
    return screenRect;
}

QPixmap MainWindow::toRoundedPixmap(QPixmap pixmap, int radius) const
{
    if (pixmap.isNull())
        return pixmap;
    QPixmap tmp(pixmap.size());
    tmp.fill(Qt::transparent);
    QPainter painter(&tmp);
    painter.setRenderHints(QPainter::Antialiasing, true);
    painter.setRenderHints(QPainter::SmoothPixmapTransform, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, tmp.width(), tmp.height(), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, tmp.width(), tmp.height(), pixmap);
    return tmp;
}

/**
 * 通过直播间ID切换勋章
 * 这个是旧的API，但是好像有些勋章拿不到？
 */
void MainWindow::switchMedalToRoom(qint64 targetRoomId)
{
    qInfo() << "自动切换勋章：roomId=" << targetRoomId;
    QString url = "https://api.live.bilibili.com/fans_medal/v1/FansMedal/get_list_in_room";
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("切换勋章返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        QJsonArray medals = json.value("data").toArray();
        /*  buff_msg: "(舰长buff：上限提升至150%)"
            can_delete: false     // 变灰了才能删除
            day_limit: 250000
            guard_level: 3
            guard_type: 3
            icon_code: 2
            icon_text: "最近获得"
            intimacy: 1380
            is_lighted: 1         // 是否未变灰
            is_receive: 1
            last_wear_time: 1613491199
            level: 21
            live_stream_status: 0  // 是否正在直播
            lpl_status: 0
            master_available: 1
            master_status: 0
            medal_color: 1725515
            medal_color_border: 6809855
            medal_color_end: 5414290
            medal_color_start: 1725515
            medal_id: 373753
            medal_level: 21
            medal_name: "181mm"
            next_intimacy: 2000
            rank: "-"
            receive_channel: 4
            receive_time: "2021-01-10 09:33:22"
            room_id: 11584296      // 牌子房间
            score: 50001380
            source: 1
            status: 1              // 是否佩戴中
            sup_code: 2
            sup_text: "最近获得"
            target_face: ""
            target_id: 20285041
            target_name: "懒一夕智能科技"
            today_feed: 0
            today_intimacy: 0
            uid: 20285041          // 牌子用户
        */

        foreach (QJsonValue val, medals)
        {
            QJsonObject medal = val.toObject();
            qint64 roomId = static_cast<qint64>(medal.value("room_id").toDouble());
            int status = medal.value("status").toInt(); // 1佩戴，0未佩戴

            if (roomId == targetRoomId)
            {
                if (status) // 已佩戴，就不用管了
                    return ;

                // 佩带牌子
                /*int isLighted = medal.value("is_lighted").toBool(); // 1能用，0变灰
                if (!isLighted) // 牌子是灰的，可以不用管，发个弹幕就点亮了
                    return ;
                */

                qint64 medalId = static_cast<qint64>(medal.value("medal_id").toDouble());
                wearMedal(medalId);
                return ;
            }
        }
        qWarning() << "第一页未检测到粉丝勋章，无法自动切换";
    });
}

/**
 * 通过主播ID切换勋章
 */
void MainWindow::switchMedalToUp(qint64 upId, int page)
{
    qInfo() << "自动切换勋章：upId=" << upId;
    QString url = "https://api.live.bilibili.com/fans_medal/v1/fans_medal/get_home_medals?uid=" + ac->cookieUid + "&source=2&need_rank=false&master_status=0&page=" + snum(page);
    get(url, [=](MyJson json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("切换勋章返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        MyJson data = json.data();
        JI(data, cnt); // 粉丝勋章个数
        JI(data, max); // 1000
        JI(data, curr_page);
        JI(data, total_page);
        JA(data, list);

        /*  can_delete: true
            day_limit: 250000
            guard_level: 0
            guard_type: 0
            icon_code: 0
            icon_text: ""
            intimacy: 1380
            is_lighted: 1
            is_receive: 1
            last_wear_time: 1625119143
            level: 21
            live_stream_status: 0
            lpl_status: 0
            master_available: 1
            master_status: 0
            medal_color: 1725515
            medal_color_border: 1725515
            medal_color_end: 5414290
            medal_color_start: 1725515
            medal_id: 373753
            medal_level: 21
            medal_name: "181mm"
            next_intimacy: 2000
            rank: "-"
            receive_channel: 4
            receive_time: "2021-01-10 09:33:22"
            score: 50001380
            source: 1
            status: 0
            target_face: "https://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg"
            target_id: 20285041
            target_name: "懒一夕智能科技官方"
            today_feed: 0
            today_intimacy: 0
            uid: 20285041  */
        foreach (QJsonValue val, list)
        {
            MyJson medal = val.toObject();
            JI(medal, status); // 1佩戴，0未佩戴
            JL(medal, target_id); // 主播

            if (target_id == upId)
            {
                if (status) // 已佩戴，就不用管了
                {
                    qInfo() << "已佩戴当前直播间的粉丝勋章";
                    return ;
                }

                // 佩带牌子
                /*int isLighted = medal.value("is_lighted").toBool(); // 1能用，0变灰
                if (!isLighted) // 牌子是灰的，可以不用管，发个弹幕就点亮了
                    return ;
                */

                qint64 medalId = static_cast<qint64>(medal.value("medal_id").toDouble());
                wearMedal(medalId);
                return ;
            }
        }

        // 未检测到勋章
        if (curr_page >= total_page) // 已经是最后一页了
        {
            qInfo() << "未检测到勋章，无法佩戴";
        }
        else // 没有勋章
        {
            switchMedalToUp(upId, page+1);
        }
    });
}

void MainWindow::wearMedal(qint64 medalId)
{
    qInfo() << "佩戴勋章：medalId=" << medalId;
    QString url("https://api.live.bilibili.com/xlive/web-room/v1/fansMedal/wear");
    QStringList datas;
    datas << "medal_id=" + QString::number(medalId);
    datas << "csrf_token=" + ac->csrf_token;
    datas << "csrf=" + ac->csrf_token;
    datas << "visit_id=";
    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    post(url, ba, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("戴勋章返回结果不为0：") << json.value("message").toString();
            return ;
        }
        qInfo() << "佩戴主播粉丝勋章成功";
    });
}

/**
 * 自动签到
 */
void MainWindow::doSign()
{
    if (ac->csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("机器人账号未登录");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("每日自动签到");
        });
        return ;
    }

    QString url("https://api.live.bilibili.com/xlive/web-ucenter/v1/sign/DoSign");

    // 建立对象
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("自动签到返回结果不为0：") << msg;
            ui->autoDoSignCheck->setText(msg);
        }
        else
        {
            ui->autoDoSignCheck->setText("签到成功");
            ui->autoDoSignCheck->setToolTip("最近签到时间：" + QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss"));
        }
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("每日自动签到");
        });
    });
}

void MainWindow::joinLOT(qint64 id, bool follow)
{
    if (!id )
        return ;
    if (ac->csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("机器人账号未登录");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("自动参与活动");
        });
        return ;
    }

    QString url("https://api.live.bilibili.com/xlive/lottery-interface/v1/Anchor/Join"
             "?id="+QString::number(id)+(follow?"&follow=true":"")+"&platform=pc&csrf_token="+ac->csrf_token+"&csrf="+ac->csrf_token+"&visit_id=");
    qInfo() << "参与天选：" << id << follow << url;

    post(url, QByteArray(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("参加天选返回结果不为0：") << msg;
            ui->autoLOTCheck->setText(msg);
            ui->autoLOTCheck->setToolTip(msg);
        }
        else
        {
            ui->autoLOTCheck->setText("参与成功");
            qInfo() << "参与天选成功！";
            ui->autoLOTCheck->setToolTip("最近参与时间：" + QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss"));
        }
        QTimer::singleShot(10000, [=]{
            ui->autoLOTCheck->setText("自动参与活动");
        });
    });
}

void MainWindow::joinStorm(qint64 id)
{
    if (!id )
        return ;
    if (ac->csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("机器人账号未登录");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("自动参与活动");
        });
        return ;
    }

    QString url("https://api.live.bilibili.com/xlive/lottery-interface/v1/storm/Join"
             "?id="+QString::number(id)+"&color=5566168&csrf_token="+ac->csrf_token+"&csrf="+ac->csrf_token+"&visit_id=");
    qInfo() << "参与节奏风暴：" << id << url;

    post(url, QByteArray(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("参加节奏风暴返回结果不为0：") << msg;
            ui->autoLOTCheck->setText(msg);
            ui->autoLOTCheck->setToolTip(msg);
        }
        else
        {
            ui->autoLOTCheck->setText("参与成功");
            qInfo() << "参与节奏风暴成功！";
            QString content = json.value("data").toObject().value("content").toString();
            ui->autoLOTCheck->setToolTip("最近参与时间：" +
                                            QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss")
                                            + "\n\n" + content);
        }
        QTimer::singleShot(10000, [=]{
            ui->autoLOTCheck->setText("自动参与活动");
        });
    });
}

void MainWindow::sendPrivateMsg(qint64 uid, QString msg)
{
    if (ac->csrf_token.isEmpty())
    {
        return ;
    }

    QString url("https://api.vc.bilibili.com/web_im/v1/web_im/send_msg");

    QStringList params;
    params << "msg%5Bsender_uid%5D=" + ac->cookieUid;
    params << "msg%5Breceiver_id%5D=" + snum(uid);
    params << "msg%5Breceiver_type%5D=1";
    params << "msg%5Bmsg_type%5D=1";
    params << "msg%5Bmsg_status%5D=0";
    params << "msg%5Bcontent%5D=" + QUrl::toPercentEncoding("{\"content\":\"" + msg.replace("\n", "\\n") + "\"}");
    params << "msg%5Btimestamp%5D="+snum(QDateTime::currentSecsSinceEpoch());
    params << "msg%5Bnew_face_version%5D=0";
    params << "msg%5Bdev_id%5D=81872DC0-FBC0-4CF8-8E93-093DE2083F51";
    params << "from_firework=0";
    params << "build=0";
    params << "mobi_app=web";
    params << "csrf_token=" + ac->csrf_token;
    params << "csrf=" + ac->csrf_token;
    QByteArray ba(params.join("&").toStdString().data());

    // 连接槽
    post(url, ba, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("发送消息出错，返回结果不为0：") << msg;
            return ;
        }
    });
}

void MainWindow::joinBattle(int type)
{
    if (!isLiving() || ac->cookieUid != ac->upUid)
    {
        showError("大乱斗", "未开播或不是主播本人");
        return ;
    }

    QStringList params{
        "room_id", ac->roomId,
        "platform", "pc",
        "battle_type", snum(type),
        "csrf_token", ac->csrf_token,
        "csrf", ac->csrf_token
    };
    post("https://api.live.bilibili.com/av/v1/Battle/join", params, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
            showError("开启大乱斗", json.value("message").toString());
        else if (danmakuWindow)
            danmakuWindow->setStatusText("正在匹配...");
        qInfo() << "匹配大乱斗" << json;
    });
}

/**
 * 监听勋章升级
 * 一个小问题：如果用户一点一点的点击送礼物，那么升级那段时间获取到的亲密度刚好在送礼物边缘
 * 可能会多播报几次，或者压根就不播报
 */
void MainWindow::detectMedalUpgrade(LiveDanmaku danmaku)
{
    /* {
        "code": 0,
        "msg": "",
        "message": "",
        "data": {
            "guard_type": 3,
            "intimacy": 2672,
            "is_receive": 1,
            "last_wear_time": 1616941910,
            "level": 23,
            "lpl_status": 0,
            "master_available": 1,
            "master_status": 0,
            "medal_id": 37075,
            "medal_name": "蘑菇云",
            "receive_channel": 30726000,
            "receive_time": "2020-12-11 21:41:39",
            "score": 50007172,
            "source": 1,
            "status": 0,
            "target_id": 13908357,
            "today_intimacy": 4,
            "uid": 20285041,
            "target_name": "娇羞的蘑菇",
            "target_face": "https://i1.hdslb.com/bfs/face/180d0e87a0e88cb6c04ce6504c3f04003dd77392.jpg",
            "live_stream_status": 0,
            "icon_code": 0,
            "icon_text": "",
            "rank": "-",
            "medal_color": 1725515,
            "medal_color_start": 1725515,
            "medal_color_end": 5414290,
            "guard_level": 3,
            "medal_color_border": 6809855,
            "is_lighted": 1,
            "today_feed": 4,
            "day_limit": 250000,
            "next_intimacy": 3000,
            "can_delete": false
        }
    } */
    // 如果是一点一点的点过去，则会出问题
    qint64 uid = danmaku.getUid();
    if (medalUpgradeWaiting.contains(uid)) // 只计算第一次
        return ;

    QList<int> specialGifts { 30607 };
    if (ac->upUid.isEmpty() || (!danmaku.getTotalCoin() && !specialGifts.contains(danmaku.getGiftId()))) // 亲密度为0，不需要判断
    {
        if (debugPrint)
            localNotify("[勋章升级：免费礼物]");
        return ;
    }
    int giftIntimacy = danmaku.getTotalCoin() / 100;
    if (danmaku.getGiftId() == 30607)
    {
        if ((danmaku.getAnchorRoomid() == ac->roomId && danmaku.getMedalLevel() < 21) || !danmaku.isGuard())
        {
            giftIntimacy = danmaku.getNumber() * 50; // 21级以下的小心心有效，一个50
        }
        else
        {
            if (debugPrint)
                localNotify("[勋章升级：小心心无效]");
            return ;
        }
    }
    if (!giftIntimacy) // 0瓜子，不知道什么小礼物，就不算进去了
    {
        if (debugPrint)
            localNotify("[勋章升级：0电池礼物]");
        return ;
    }

    QString currentAnchorRoom = danmaku.getAnchorRoomid();
    int currentMedalLevel = danmaku.getMedalLevel();
    if (debugPrint)
        localNotify("[当前勋章：房间" + currentAnchorRoom + "，等级" + snum(currentMedalLevel) + "]");

    // 获取新的等级
    QString url = "https://api.live.bilibili.com/fans_medal/v1/fans_medal/get_fans_medal_info?source=1&uid="
            + snum(danmaku.getUid()) + "&target_id=" + ac->upUid;

    medalUpgradeWaiting.append(uid);
    QTimer::singleShot(0, [=]{
        medalUpgradeWaiting.removeOne(uid);
        get(url, [=](MyJson json){
            MyJson medalObject = json.data();
            if (medalObject.isEmpty())
            {
                if (debugPrint)
                    localNotify("[勋章升级：无勋章]");
                return ; // 没有勋章，更没有亲密度
            }
            int intimacy = medalObject.i("intimacy"); // 当前亲密度
            int nextIntimacy = medalObject.i("next_intimacy"); // 下一级亲密度
            if (debugPrint)
                localNotify("[亲密度：" + snum(intimacy) + "/" + snum(nextIntimacy) + "]");
            if (intimacy >= giftIntimacy) // 没有升级，或者刚拿到粉丝牌升到1级
            {
                if (debugPrint)
                    localNotify("[勋章升级：未升级，已有" + snum(intimacy) + ">=礼物" + snum(giftIntimacy) + "]");
                return ;
            }
            LiveDanmaku ld = danmaku;
            int level = medalObject.i("level");
            if (debugPrint)
            {
                localNotify("[勋章升级：" + snum(level) + "级]");
            }
            if (ld.getAnchorRoomid() != ac->roomId && (!ac->shortId.isEmpty() && ld.getAnchorRoomid() != ac->shortId)) // 没有戴本房间的牌子
            {
                if (debugPrint)
                    localNotify("[勋章升级：非本房间 " + ld.getAnchorRoomid() + "]");
            }
            ld.setMedalLevel(level); // 设置为本房间的牌子
            triggerCmdEvent("MEDAL_UPGRADE", ld.with(json), true);
        });
    });
}

/// 根据UL等级或者舰长自动调整字数
void MainWindow::adjustDanmakuLongest()
{
    int longest = 20;
    // UL等级：20级30字
    if (ac->cookieULevel >= 20)
        longest = qMax(longest, 30);

    // 大航海：舰长20，提督/总督40
    if (ac->cookieGuardLevel == 1 || ac->cookieGuardLevel == 2)
        longest = qMax(longest, 40);

    ui->danmuLongestSpin->setValue(longest);
    on_danmuLongestSpin_editingFinished();
}

void MainWindow::myLiveSelectArea(bool update)
{
    get("https://api.live.bilibili.com/xlive/web-interface/v1/index/getWebAreaList?source_id=2", [=](MyJson json) {
        if (json.code() != 0)
            return showError("获取分区列表", json.msg());
        newFacileMenu->setSubMenuShowOnCursor(false);
        if (update)
            menu->addTitle("修改分区", 1);
        else
            menu->addTitle("选择分区", 1);

        json.data().each("data", [=](MyJson json){
            QString parentId = snum(json.i("id")); // 这是int类型的
            QString parentName = json.s("name");
            auto parentMenu = menu->addMenu(parentName);
            json.each("list", [=](MyJson json) {
                QString id = json.s("id"); // 这个是字符串的
                QString name = json.s("name");
                parentMenu->addAction(name, [=]{
                    ac->areaId = id;
                    ac->areaName = name;
                    ac->parentAreaId = parentId;
                    ac->parentAreaName = parentName;
                    us->setValue("myLive/areaId", ac->areaId);
                    us->setValue("myLive/parentAreaId", ac->parentAreaId);
                    qInfo() << "选择分区：" << parentName << parentId << ac->areaName << ac->areaId;
                    if (update)
                    {
                        myLiveUpdateArea(ac->areaId);
                    }
                });
            });
        });

        menu->exec();
    });
}

void MainWindow::myLiveUpdateArea(QString area)
{
    qInfo() << "更新AreaId:" << area;
    post("https://api.live.bilibili.com/room/v1/Room/update",
    {"room_id", ac->roomId, "area_id", area,
         "csrf_token", ac->csrf_token, "csrf", ac->csrf_token},
         [=](MyJson json) {
        if (json.code() != 0)
            return showError("修改分区失败", json.msg());
    });
}

void MainWindow::myLiveStartLive()
{
    int lastAreaId = us->value("myLive/areaId", 0).toInt();
    if (!lastAreaId)
    {
        showError("一键开播", "必须选择分类才能开播");
        return ;
    }
    post("https://api.live.bilibili.com/room/v1/Room/startLive",
    {"room_id", ac->roomId, "platform", "pc", "area_v2", snum(lastAreaId),
         "csrf_token", ac->csrf_token, "csrf", ac->csrf_token},
         [=](MyJson json) {
        qInfo() << "开播：" << json;
        if (json.code() != 0)
            return showError("一键开播失败", json.msg());
        MyJson rtmp = json.data().o("rtmp");
        myLiveRtmp = rtmp.s("addr");
        myLiveCode = rtmp.s("code");
    });
}

void MainWindow::myLiveStopLive()
{
    post("https://api.live.bilibili.com/room/v1/Room/stopLive",
    {"room_id", ac->roomId, "platform", "pc",
         "csrf_token", ac->csrf_token, "csrf", ac->csrf_token},
         [=](MyJson json) {
        qInfo() << "下播：" << json;
        if (json.code() != 0)
            return showError("一键下播失败", json.msg());
    });
}

void MainWindow::myLiveSetTitle(QString newTitle)
{
    if (ac->upUid != ac->cookieUid)
        return showError("只有主播才能操作");
    if (newTitle.isEmpty())
    {
        QString title = ac->roomTitle;
        bool ok = false;
        newTitle = QInputDialog::getText(this, "修改直播间标题", "修改直播间标题，立刻生效", QLineEdit::Normal, title, &ok);
        if (!ok)
            return ;
    }

    post("https://api.live.bilibili.com/room/v1/Room/update",
         QStringList{
             "room_id", ac->roomId,
             "title", newTitle,
             "csrf_token", ac->csrf_token,
             "csrf", ac->csrf_token
         }, [=](MyJson json) {
        qInfo() << "设置直播间标题：" << json;
        if (json.code() != 0)
            return showError(json.msg());
        ac->roomTitle = newTitle;
        ui->roomNameLabel->setText(newTitle);
#ifdef ZUOQI_ENTRANCE
        fakeEntrance->setRoomName(newTitle);
#endif
    });
}

void MainWindow::myLiveSetNews()
{
    QString content = ac->roomNews;
    bool ok = false;
    content = TextInputDialog::getText(this, "修改主播公告", "修改主播公告，立刻生效", content, &ok);
    if (!ok)
        return ;

    post("https://api.live.bilibili.com/room_ex/v1/RoomNews/update",
         QStringList{
             "room_id", ac->roomId,
             "uid", ac->cookieUid,
             "content", content,
             "csrf_token", ac->csrf_token,
             "csrf", ac->csrf_token
         }, [=](MyJson json) {
        qInfo() << "设置主播公告：" << json;
        if (json.code() != 0)
            return showError(json.msg());
        ac->roomNews = content;
    });
}

void MainWindow::myLiveSetDescription()
{
    QString content = ac->roomDescription;
    bool ok = false;
    content = TextInputDialog::getText(this, "修改个人简介", "修改主播的个人简介，立刻生效", content, &ok);
    if (!ok)
        return ;

    post("https://api.live.bilibili.com/room/v1/Room/update",
         QStringList{
             "room_id", ac->roomId,
             "description", content,
             "csrf_token", ac->csrf_token,
             "csrf", ac->csrf_token
         }, [=](MyJson json) {
        qInfo() << "设置个人简介：" << json;
        if (json.code() != 0)
            return showError(json.msg());
        ac->roomDescription = content;
        ui->roomDescriptionBrowser->setPlainText(ac->roomDescription);
    });
}

void MainWindow::myLiveSetCover(QString path)
{
    if (ac->upUid != ac->cookieUid)
        return showError("只有主播才能操作");
    if (path.isEmpty())
    {
        // 选择封面
        QString oldPath = us->value("recent/coverPath", "").toString();
        path = QFileDialog::getOpenFileName(this, "选择上传的封面", oldPath, "Image (*.jpg *.png *.jpeg *.gif)");
        if (path.isEmpty())
            return ;
        us->setValue("recent/coverPath", path);
    }
    else
    {
        if (!isFileExist(path))
            return showError("封面文件不存在", path);
    }

    // 裁剪图片：大致是 470 / 293 = 1.6 = 8 : 5
    QPixmap pixmap(path);
    // if (!ClipDialog::clip(pixmap, AspectRatio, 8, 5))
    //     return ;

    // 压缩图片
    const int width = 470;
    const QString clipPath = rt->dataPath + "temp_cover.jpeg";
    pixmap = pixmap.scaledToWidth(width, Qt::SmoothTransformation);
    pixmap.save(clipPath);

    // 开始上传
    HttpUploader* uploader = new HttpUploader("https://api.bilibili.com/x/upload/web/image?csrf=" + ac->csrf_token);
    uploader->setCookies(ac->userCookies);
    uploader->addTextField("bucket", "live");
    uploader->addTextField("dir", "new_room_cover");
    uploader->addFileField("file", "blob", clipPath, "image/jpeg");
    uploader->post();
    connect(uploader, &HttpUploader::finished, this, [=](QNetworkReply* reply) {
        MyJson json(reply->readAll());
        qInfo() << "上传封面：" << json;
        if (json.code() != 0)
            return showError("上传封面失败", json.msg());

        auto data = json.data();
        QString location = data.s("location");
        QString etag = data.s("etag");

        if (roomCover.isNull()) // 仅第一次上传封面，调用 add
        {
            post("https://api.live.bilibili.com/room/v1/Cover/add",
            {"room_id", ac->roomId,
             "url", location,
             "type", "cover",
             "csrf_token", ac->csrf_token,
             "csrf", ac->csrf_token,
             "visit_id", getRandomKey(12)
                 }, [=](MyJson json) {
                qInfo() << "添加封面：" << json;
                if (json.code() != 0)
                    return showError("添加封面失败", json.msg());
            });
        }
        else // 后面就要调用替换的API了，需要参数 pic_id
        {
            // 获取 pic_id
            get("https://api.live.bilibili.com/room/v1/Cover/new_get_list?room_id=" +ac->roomId, [=](MyJson json) {
                qInfo() << "获取封面ID：" << json;
                if (json.code() != 0)
                    return showError("设置封面失败", "无法获取封面ID");
                auto array = json.a("data");
                if (!array.size())
                    return showError("设置封面失败", "获取不到封面数据");
                const qint64 picId = (long long)(array.first().toObject().value("id").toDouble());

                post("https://api.live.bilibili.com/room/v1/Cover/new_replace_cover",
                {"room_id", ac->roomId,
                 "url", location,
                 "pic_id", snum(picId),
                 "type", "cover",
                 "csrf_token", ac->csrf_token,
                 "csrf", ac->csrf_token,
                 "visit_id", getRandomKey(12)
                     }, [=](MyJson json) {
                    qInfo() << "设置封面：" << json;
                    if (json.code() != 0)
                        return showError("设置封面失败", json.msg());
                });
            });
        }

    });
}

void MainWindow::myLiveSetTags()
{
    QString content = ac->roomTags.join(" ");
    bool ok = false;
    content = QInputDialog::getText(this, "修改我的个人标签", "多个标签之间使用空格分隔\n短时间修改太多标签可能会被临时屏蔽", QLineEdit::Normal, content, &ok);
    if (!ok)
        return ;

    QStringList oldTags = ac->roomTags;
    QStringList newTags = content.split(" ", QString::SkipEmptyParts);

    auto toPost = [=](QString action, QString tag){
        QString rst = NetUtil::postWebData("https://api.live.bilibili.com/room/v1/Room/update",
                             QStringList{
                                 "room_id", ac->roomId,
                                 action, tag,
                                 "csrf_token", ac->csrf_token,
                                 "csrf", ac->csrf_token
                             }, ac->userCookies);
        MyJson json(rst.toUtf8());
        qInfo() << "修改个人标签：" << json;
        if (json.code() != 0)
            return showError(json.msg());
        if (action == "add_tag")
            ac->roomTags.append(tag);
        else
            ac->roomTags.removeOne(tag);
    };

    // 对比新旧
    foreach (auto tag, newTags)
    {
        if (oldTags.contains(tag)) // 没变的部分
        {
            oldTags.removeOne(tag);
            continue;
        }

        // 新增的
        qInfo() << "添加个人标签：" << tag;
        toPost("add_tag", tag);
    }
    foreach (auto tag, oldTags)
    {
        qInfo() << "删除个人标签：" << tag;
        toPost("del_tag", tag);
    }

    // 刷新界面
    ui->tagsButtonGroup->initStringList(ac->roomTags);
}

void MainWindow::showPkMenu()
{
    newFacileMenu;

    menu->addAction("大乱斗规则", [=]{
        if (pkRuleUrl.isEmpty())
        {
            showError("大乱斗规则", "未找到规则说明");
            return ;
        }
        QDesktopServices::openUrl(QUrl(pkRuleUrl));
    });

    menu->addAction("最佳助攻列表", [=]{
        showPkAssists();
    });

    menu->split()->addAction("赛季匹配记录", [=]{
        showPkHistories();
    });

    menu->addAction("最后匹配的直播间", [=]{
        if (!ac->lastMatchRoomId)
        {
            showError("匹配记录", "没有最后匹配的直播间");
            return ;
        }
        QDesktopServices::openUrl(QUrl("https://live.bilibili.com/" + snum(ac->lastMatchRoomId)));
    });

    menu->exec();
}

void MainWindow::showPkAssists()
{
    QString url = "https://api.live.bilibili.com/av/v1/Battle/anchorBattleRank?uid=" + ac->upUid + "&room_id=" + ac->roomId + "&_=" + snum(QDateTime::currentMSecsSinceEpoch());
    // qInfo() << "pk assists:" << url;
    get(url, [=](MyJson json) {
        JO(json, data);
        JA(data, assist_list); // 助攻列表

        QString title = "助攻列表";
        auto view = new QTableView(this);
        auto model = new QStandardItemModel(view);
        QStringList columns {
            "名字", "积分", "UID"
        };
        model->setColumnCount(columns.size());
        model->setHorizontalHeaderLabels(columns);
        model->setRowCount(assist_list.size());


        for (int row = 0; row < assist_list.count(); row++)
        {
            MyJson assist = assist_list.at(row).toObject();
            JI(assist, rank);
            JL(assist, uid);
            JS(assist, name);
            JS(assist, face);
            JL(assist, pk_score);

            int col = 0;
            model->setItem(row, col++, new QStandardItem(name));
            model->setItem(row, col++, new QStandardItem(snum(pk_score)));
            model->setItem(row, col++, new QStandardItem(snum(uid)));
        }

        // 创建控件
        view->setModel(model);
        view->setAttribute(Qt::WA_ShowModal, true);
        view->setAttribute(Qt::WA_DeleteOnClose, true);
        view->setWindowFlags(Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | Qt::Dialog);

        // 自适应宽度
        view->resizeColumnsToContents();
        for(int i = 0; i < model->columnCount(); i++)
            view->setColumnWidth(i, view->columnWidth(i) + 10); // 加一点，不然会显得很挤

        // 显示位置
        QRect rect = this->geometry();
        // int titleHeight = style()->pixelMetric(QStyle::PM_TitleBarHeight);
        rect.setTop(rect.top());
        view->setWindowTitle(title);
        view->setGeometry(rect);
        view->show();

        // 菜单
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, view, [=]{
            auto index = view->currentIndex();
            if (!index.isValid())
                return ;
            int row = index.row();
            QString uname = model->item(row, 0)->data(Qt::DisplayRole).toString();
            QString uid = model->item(row, 2)->data(Qt::DisplayRole).toString();

            newFacileMenu;
            menu->addAction("复制昵称", [=]{
                QApplication::clipboard()->setText(uname);
            });
            menu->addAction("复制UID", [=]{
                QApplication::clipboard()->setText(uid);
            });
            menu->split()->addAction("查看首页", [=]{
                QDesktopServices::openUrl(QUrl("https://space.bilibili.com/" + uid));
            });
            menu->exec();
        });
    });
}

void MainWindow::showPkHistories()
{
    QString url = "https://api.live.bilibili.com/av/v1/Battle/getPkRecord?"
                  "ruid=" + ac->upUid + "&room_id=" + ac->roomId + "&season_id=" + snum(currentSeasonId) + "&_=" + snum(QDateTime::currentMSecsSinceEpoch());
    // qInfo() << "pk histories" << url;
    get(url, [=](MyJson json) {
        if (json.code())
            return showError("获取PK历史失败", json.msg());
        QString title = "大乱斗历史";
        auto view = new QTableView(this);
        auto model = new QStandardItemModel(view);
        int rowCount = 0;
        QStringList columns {
            "时间", "主播", "结果", "积分", "票数"/*自己:对面*/, "房间ID"
        };
        model->setColumnCount(columns.size());
        model->setHorizontalHeaderLabels(columns);

        auto pkInfo = json.data().o("pk_info");
        auto dates = pkInfo.keys();
        std::sort(dates.begin(), dates.end(), [=](const QString& s1, const QString& s2) {
            return s1 > s2;
        });
        foreach (auto date, dates)
        {
            pkInfo.o(date).each("list", [&](MyJson info) {
                int result = info.i("result_type"); // 2胜利，1平局，-1失败
                QString resultText = result < 0 ? "失败" : result > 1 ? "胜利" : "平局";
                auto matchInfo = info.o("match_info");

                int row = rowCount++;
                int col = 0;
                model->setRowCount(rowCount);
                model->setItem(row, col++, new QStandardItem(date + " " + info.s("pk_end_time")));
                model->setItem(row, col++, new QStandardItem(matchInfo.s("name")));
                auto resultItem = new QStandardItem(resultText);
                model->setItem(row, col++, resultItem);
                auto scoreItem = new QStandardItem(snum(info.l("pk_score")));
                model->setItem(row, col++, scoreItem);
                scoreItem->setTextAlignment(Qt::AlignCenter);
                auto votesItem = new QStandardItem(snum(info.l("current_pk_votes")) + " : " + snum(matchInfo.l("pk_votes")));
                model->setItem(row, col++, votesItem);
                votesItem->setTextAlignment(Qt::AlignCenter);
                if (result > 1)
                    resultItem->setForeground(Qt::green);
                else if (result < 0)
                    resultItem->setForeground(Qt::red);
                model->setItem(row, col++, new QStandardItem(snum(matchInfo.l("room_id"))));
            });
        }

        // 创建控件
        view->setModel(model);
        view->setAttribute(Qt::WA_ShowModal, true);
        view->setAttribute(Qt::WA_DeleteOnClose, true);
        view->setWindowFlags(Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | Qt::Dialog);

        // 自适应宽度
        view->resizeColumnsToContents();
        for(int i = 0; i < model->columnCount(); i++)
            view->setColumnWidth(i, view->columnWidth(i) + 10); // 加一点，不然会显得很挤

        // 显示位置
        QRect rect = this->geometry();
        // int titleHeight = style()->pixelMetric(QStyle::PM_TitleBarHeight);
        rect.setTop(rect.top());
        view->setWindowTitle(title);
        view->setGeometry(rect);
        view->show();

        // 菜单
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, view, [=]{
            auto index = view->currentIndex();
            if (!index.isValid())
                return ;
            int row = index.row();
            QString uname = model->item(row, 1)->data(Qt::DisplayRole).toString();
            QString roomId = model->item(row, 5)->data(Qt::DisplayRole).toString();

            newFacileMenu;
            menu->addAction("复制昵称", [=]{
                QApplication::clipboard()->setText(uname);
            });
            menu->addAction("复制房间号", [=]{
                QApplication::clipboard()->setText(roomId);
            });
            menu->split()->addAction("前往直播间", [=]{
                QDesktopServices::openUrl(QUrl("https://live.bilibili.com/" + roomId));
            });
            menu->exec();
        });
    });
}

void MainWindow::refreshPrivateMsg()
{
    if (ac->cookieUid.isEmpty())
        return ;

    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
    get("https://api.vc.bilibili.com/session_svr/v1/session_svr/get_sessions?session_type=1&group_fold=1&unfollow_fold=0&build=0&mobi_app=web",
    // get("https://api.vc.bilibili.com/session_svr/v1/session_svr/new_sessions?begin_ts=0&build=0&mobi_app=web",
        [=](MyJson json) {
        if (json.code() != 0)
        {
            if (json.code() == -412)
            {
                showError("接收私信：" + snum(json.code()), "过于频繁，15分钟后自动重试");
                ui->receivePrivateMsgCheck->setChecked(false);
                QTimer::singleShot(15 * 60000, [=]{
                    ui->receivePrivateMsgCheck->setChecked(true);
                });
            }
            else
            {
                showError("接收私信：" + snum(json.code()), json.msg());
                ui->receivePrivateMsgCheck->setChecked(false);
            }
            return ;
        }

        MyJson data = json.data();
        QJsonArray sessionList = data.a("session_list");
        foreach (QJsonValue sessionV, sessionList) // 默认按最新时间排序
        {
            MyJson session = sessionV.toObject();

            // 判断未读消息
            /* qint64 unreadCount = session.i("unread_count");
            if (!unreadCount) // TODO:有时候自动回复不会显示未读
                continue; */

            qint64 sessionTs = session.l("session_ts") / 1000; // 会话时间，纳秒
            if (sessionTs > currentTimestamp) // 连接中间的时间
                continue;
            if (sessionTs <= privateMsgTimestamp) // 之前已处理
                break;

            // 接收到会话信息
            receivedPrivateMsg(session);
        }
        privateMsgTimestamp = currentTimestamp;
    });
}

void MainWindow::receivedPrivateMsg(MyJson session)
{
    // 解析消息内容
    qint64 talkerId = session.l("talker_id"); // 用户ID
    if (!talkerId)
        return ;
    qint64 sessionTs = session.l("session_ts") / 1000; // 会话时间，纳秒
    int isFollow = session.i("is_follow");
    int isDnd = session.i("is_dnd");
    MyJson lastMsg = session.o("last_msg");
    QString content = "";
    int msgType = 0;
    if (!lastMsg.isEmpty())
    {
        // 1纯文本，2图片，3撤回消息，6自定义表情，7分享稿件，10通知消息，11发布视频，12发布专栏，13卡片消息，14分享直播，
        msgType = lastMsg.i("msg_type"); // 使用 %.msg_type% 获取
        qint64 senderUid = lastMsg.l("sender_uid"); // 自己或者对面发送的
        if (senderUid != talkerId) // 自己已经回复了
            return ;
        qint64 receiverId = lastMsg.l("receiver_id");
        Q_ASSERT(receiverId == ac->cookieUid.toLongLong());
        content = lastMsg.s("content");
        MyJson lastContent = MyJson::from(content.toUtf8());
        if (lastContent.contains("content"))
            content = lastContent.s("content");
        else if (lastContent.contains("text"))
            content = lastContent.s("text");
        else
            content.replace("\\n", "\n"); // 肯定有问题
    }
    qInfo() << "接收到私信：" << session;

    // 获取发送者信息
    get("https://api.bilibili.com/x/space/acc/info?mid=" + snum(talkerId), [=](MyJson info) {
        MyJson newJson = session;

        // 解析信息
        QString name = snum(talkerId);
        QString faceUrl = "";
        if (info.code() == 0)
        {
            MyJson data = info.data();
            name = data.s("name");
            faceUrl = data.s("face");
            newJson.insert("sender", info);
        }
        else
        {
            qWarning() << "获取私信发送者信息失败：" << info.msg();
        }
        qInfo() << "接收到私信: " << name << msgType << ":" << content
                << "    (" << sessionTs << "  in "
                << privateMsgTimestamp << "~" << QDateTime::currentMSecsSinceEpoch() << ")";

        // 触发事件
        LiveDanmaku danmaku(talkerId, name, content);
        danmaku.with(session);
        danmaku.setUid(talkerId);
        danmaku.setTime(QDateTime::fromMSecsSinceEpoch(sessionTs));
        triggerCmdEvent("RECEIVE_PRIVATE_MSG", danmaku);
    });
}

void MainWindow::getPositiveVote()
{
    get("https://api.live.bilibili.com/xlive/virtual-interface/v1/app/detail?app_id=1653383145397", [=](MyJson json) {
        if (json.code() != 0)
        {
            qWarning() << "获取饭贩商店应用失败：" << json;
            return ;
        }
        MyJson data = json.data();
        _fanfanOwn = data.b("own");
        _fanfanLike = data.b("is_like");
        _fanfanLikeCount = data.i("like_count");
        ui->positiveVoteCheck->setChecked(_fanfanLike);
        ui->positiveVoteCheck->setText("好评(" + snum(_fanfanLikeCount) + ")");

        // 获取到的数值可能是有问题的
        if (_fanfanLikeCount == 0)
            return ;

        // 没有评价过的老用户进行提示好评
        if ((QDateTime::currentSecsSinceEpoch() - us->l("runtime/first_use_time") > 3 * 24 * 3600)
                && !us->value("ask/positiveVote", false).toBool()
                && _fanfanLikeCount > 0)
        {
            QTimer::singleShot(3000, [=]{
                if (!ac->cookieUid.isEmpty() && !_fanfanLike)
                {
                    auto noti = new NotificationEntry("positiveVote", "好评", "您是否觉得神奇弹幕好用？");
                    noti->setBtn(1, "好评", "god");
                    noti->setBtn(2, "吐槽", "bad");
                    connect(noti, &NotificationEntry::signalCardClicked, this, [=] {
                        qInfo() << "点击好评卡片";
                        positiveVote();
                    });
                    connect(noti, &NotificationEntry::signalBtnClicked, this, [=](int i) {
                        qInfo() << "点击评价按钮" << i;
                        if (i != 2)
                        {
                            us->setValue("ask/positiveVote", true);
                            positiveVote();
                        }
                        else
                            openLink("http://live.lyixi.com/");
                    });
                    connect(noti, &NotificationEntry::signalTimeout, this, [=] {
                        qInfo() << "默认好评";
                        positiveVote();
                    });
                    noti->setDefaultBtn(1);
                    tip_box->createTipCard(noti);
                }
            });
        }

        // 添加到自己的库中
        if (!_fanfanOwn)
        {
            qInfo() << "饭贩商店获取本应用";
            fanfanAddOwn();
        }
    });
}

/**
 * 饭贩好评
 * 需要 Cookie 带有 PEA_AU 字段
 */
void MainWindow::positiveVote()
{
    QString url = "https://api.live.bilibili.com/xlive/virtual-interface/v1/app/like";
    MyJson json;
    json.insert("app_id", (qint64)1653383145397);
    json.insert("csrf_token", ac->csrf_token);
    json.insert("csrf", ac->csrf_token);
    json.insert("visit_id", "");

    postJson(url, json.toBa(), [=](QNetworkReply* reply){
        QByteArray ba(reply->readAll());
        MyJson json(ba);
        if (json.code() != 0)
        {
            qWarning() << "饭贩好评失败";
            QDesktopServices::openUrl(QUrl("https://play-live.bilibili.com/details/1653383145397"));
        }

        QTimer::singleShot(0, [=]{
            getPositiveVote();
        });
    });
}

void MainWindow::fanfanLogin()
{
    post("https://api.live.bilibili.com/xlive/virtual-interface/v1/user/login", "", [=](QNetworkReply* reply){
        QVariant variantCookies = reply->header(QNetworkRequest::SetCookieHeader);
        QList<QNetworkCookie> cookies = qvariant_cast<QList<QNetworkCookie> >(variantCookies);
        if (!cookies.size())
        {
            qWarning() << "login 没有返回 set cookie。" << variantCookies;
            return ;
        }
        QNetworkCookie cookie = cookies.at(0);
        QString DataAsString =cookie.toRawForm(); // 转换为QByteArray
        qInfo() << "饭贩连接：" << DataAsString;
        autoSetCookie(ac->browserCookie + "; " + DataAsString);

        QTimer::singleShot(3000, [=]{
            if (!_fanfanLike)
            {
                positiveVote();
            }
        });
    });
}

void MainWindow::fanfanAddOwn()
{
    QString url = "https://api.live.bilibili.com/xlive/virtual-interface/v1/app/addOwn";
    MyJson json;
    json.insert("app_id", (qint64)1653383145397);
    json.insert("csrf_token", ac->csrf_token);
    json.insert("csrf", ac->csrf_token);
    json.insert("visit_id", "");

    postJson(url, json.toBa(), [=](QNetworkReply* reply){
        QByteArray ba(reply->readAll());
        MyJson json(ba);
        if (json.code() == 1666501)
        {
            qInfo() << "饭贩已拥有，无需重复添加";
        }
        else if (json.code() != 0)
        {
            qWarning() << "饭贩添加失败：" << json.msg();
        }
    });
}

void MainWindow::startSplash()
{
#ifndef Q_OS_ANDROID
    if (!ui->startupAnimationCheck->isChecked())
        return ;
    RoundedAnimationLabel* label = new RoundedAnimationLabel(this);
    QMovie* movie = new QMovie(":/icons/star_gif");
    movie->setBackgroundColor(Qt::white);
    label->setMovie(movie);
    label->setStyleSheet("background-color: white;");
    label->setGeometry(this->rect());
    label->setAlignment(Qt::AlignCenter);
    int minSize = qMin(label->width(), label->height());
    movie->setScaledSize(QSize(minSize, minSize));
    int lastFrame = movie->frameCount()-1;
    connect(movie, &QMovie::frameChanged, label, [=](int frameNumber){
        if (frameNumber < lastFrame)
            return ;

        movie->stop();
        QPixmap pixmap(label->size());
        label->render(&pixmap);

        // 开始隐藏
        label->startHide();
        movie->deleteLater();
    });
    label->show();
    movie->start();
#endif
}

void MainWindow::loadWebExtensionList()
{
    /* if (rt->asPlugin)
        return ; */

    // 清空旧的列表
    for (int i = 0; i < ui->extensionListWidget->count(); i++)
    {
        auto item = ui->extensionListWidget->item(i);
        auto widget = ui->extensionListWidget->itemWidget(item);
        widget->deleteLater();
    }
    ui->extensionListWidget->clear();

    // 设置界面值
    QFontMetrics fm(font());
    const int lineHeight = fm.lineSpacing();
    const int btnSize = lineHeight * 3 / 2;
    QFont titleFont;
    titleFont.setPointSize(titleFont.pointSize() * 5 / 4);
    titleFont.setBold(true);

    // 加载url列表，允许一键复制
    QList<QFileInfo> dirs = QDir(wwwDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    foreach (auto info, dirs)
    {
        QString infoPath = QDir(info.absoluteFilePath()).absoluteFilePath("package.json");
        if (!isFileExist(infoPath))
            infoPath = QDir(info.absoluteFilePath()).absoluteFilePath("info.json");
        if (!isFileExist(infoPath))
            continue;
        QString str = readTextFileAutoCodec(infoPath);
        MyJson json(str.toUtf8());
        QString extName = json.s("name");
        QString authorAll = json.s("author");
        QString descAll = json.s("desc");
        QString minVersion = json.s("min_version");
        if (!minVersion.isEmpty() && rt->appVersion < minVersion)
        {
            showError("扩展不可用", "【" + extName + "】要求版本：v" + minVersion);
        }
        json.each("list", [=](MyJson inf){
            /// 读取数值
            QString name = inf.s("name");
            QString author= inf.s("author");
            QString urlR = inf.s("url"); // 如果是 /开头，则是相对于www的路径，否则相对于当前文件夹的路径
            QString stsR = inf.s("config");
            QString cssR = inf.s("css");
            QString cssC = inf.s("css_custom");
            QString dirR = inf.s("dir");
            QString fileR = inf.s("file");
            QString desc = inf.s("desc");
            QStringList cmds = inf.ss("cmds");
            QJsonValue code = inf.value("code");
            QString coverPath = inf.s("cover");
            QString homepage = inf.s("homepage"); // TODO
            QString reward = inf.s("reward"); // TODO
            if (coverPath.isEmpty())
            {
                if (isFileExist(QDir(info.absoluteFilePath()).absoluteFilePath(coverPath = "cover.png"))
                        || isFileExist(QDir(info.absoluteFilePath()).absoluteFilePath(coverPath = "cover.jpg")))
                    coverPath = QDir(info.absoluteFilePath()).absoluteFilePath(coverPath);
                else
                    coverPath = ":/icons/gray_cover";
            }
            else
            {
                if (isFileExist(coverPath = QDir(info.absoluteFilePath()).absoluteFilePath(coverPath)))
                    ;
                else
                    coverPath = ":/icons/gray_cover";
            }
            if (name.isEmpty())
                name = extName;
            if (author.isEmpty())
                author = authorAll;
            if (desc.isEmpty())
                desc = descAll;
            QString dirName = info.fileName();
            if (!urlR.isEmpty() && !urlR.startsWith("/"))
                urlR = "/" + dirName + "/" + urlR;
            if (!cssR.isEmpty() && !cssR.startsWith("/"))
                cssR = "/" + dirName + "/" + cssR;
            if (!cssC.isEmpty() && !cssC.startsWith("/"))
                cssC = "/" + dirName + "/" + cssC;
            else if (cssC.isEmpty())
                cssC = cssR;
            if (!dirR.isEmpty() && !dirR.startsWith("/"))
                dirR = "/" + dirName + "/" + dirR;
            if (!fileR.isEmpty() && !fileR.startsWith("/"))
                fileR = "/" + dirName + "/" + fileR;
            if (!stsR.isEmpty() && !stsR.startsWith("/"))
                stsR = "/" + dirName + "/" + stsR;

            if (name.isEmpty() && urlR.isEmpty())
                return;

            /// 创建控件
            const int labelCount = 4;
            const int cardHeight = (lineHeight + 9) * labelCount;
            const int coverSize = lineHeight * 4;
            // 主卡片
            auto widget = new InteractiveButtonBase(ui->serverUrlsCard);
            widget->setPaddings(8);
            widget->setAlign(Qt::AlignLeft);
            widget->setRadius(rt->fluentRadius);
            widget->setFixedForePos();
            if (!desc.isEmpty())
                widget->setToolTip(desc);
            widget->setCursor(Qt::PointingHandCursor);

            // 封面
            auto coverVLayout = new QVBoxLayout;
            QLabel* coverLabel = new RoundedPixmapLabel(widget);
            coverLabel->setFixedSize(coverSize, coverSize);
            coverVLayout->addWidget(coverLabel);
            coverLabel->setPixmap(QPixmap(coverPath));
            coverLabel->setScaledContents(true);

            if (!author.isEmpty())
            {
                QLabel* label = new QLabel("by " + author, widget);
                label->setAlignment(Qt::AlignCenter);
                label->setStyleSheet("color: grey;");
                coverVLayout->addWidget(label);
            }

            // 信息
            auto infoVLayout = new QVBoxLayout;
            QLabel* nameLabel = new QLabel(name, widget);
            nameLabel->setFont(titleFont);
            infoVLayout->addWidget(nameLabel);
            if (!desc.isEmpty())
            {
                QLabel* label = new QLabel(desc, widget);
                label->setStyleSheet("color: grey;");
                infoVLayout->addWidget(label);
            }
            if (!urlR.isEmpty())
            {
                QLabel* label = new QLabel(urlR, widget);
                label->setStyleSheet("color: grey;");
                infoVLayout->addWidget(label);
            }
            infoVLayout->setSpacing(4);

            // 布局
            auto mainHLayout = new QHBoxLayout(widget);
            widget->setLayout(mainHLayout);
            mainHLayout->addLayout(coverVLayout);
            mainHLayout->addLayout(infoVLayout);
            mainHLayout->setSpacing(12);
            mainHLayout->setMargin(9);
            mainHLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            mainHLayout->setStretch(0, 0);
            mainHLayout->setStretch(1, 1);
            widget->setFixedHeight(cardHeight);

            /// 交互按钮
            auto btnHLayout = new QHBoxLayout;
            infoVLayout->addLayout(btnHLayout);

            // 占位符
            auto placehold = new QWidget(widget);
            placehold->setFixedSize(1, btnSize);
            btnHLayout->addWidget(placehold);

            // 右键菜单
            connect(widget, &InteractiveButtonBase::rightClicked, this, [=]{
                newFacileMenu;
                menu->addAction(QIcon(":/icons/directory"), "打开文件夹", [=]{
                    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
                });

                QString url = inf.s("homepage");
                menu->split()->addAction(QIcon(":/icons/room"), "插件主页", [=]{
                    openLink(url);
                })->disable(url.isEmpty());

                url = inf.s("contact");
                menu->addAction(QIcon(":/icons/music_reply"), "联系作者", [=]{
                    openLink(url);
                })->disable(url.isEmpty());

                url = inf.s("reward");
                menu->addAction(QIcon(":/icons/candy"), "打赏", [=]{
                    openLink(url);
                })->disable(url.isEmpty());

                // 自定义菜单
                QJsonArray customMenus = inf.a("menus");
                if (customMenus.size())
                    menu->split();
                for (QJsonValue cm: customMenus)
                {
                    MyJson mn = cm.toObject();
                    QString name = mn.s("name");
                    QString url = mn.s("url");
                    QString icon = mn.s("icon");
                    QString code = mn.s("code");
                    if (!icon.startsWith(":"))
                        icon = QDir(info.absoluteFilePath()).absoluteFilePath(icon);
                    menu->addAction(QIcon(icon), name, [=]{
                        if (!code.isEmpty())
                        {
                            sendVariantMsg(code, LiveDanmaku(), NOTIFY_CD_CN, true);
                        }
                        if (!url.isEmpty())
                            openLink(url);
                    });
                }

                menu->exec();
            });

            // URL
            if (!urlR.isEmpty())
            {
                connect(widget, &InteractiveButtonBase::clicked, this, [=]{
                    if (!ui->serverCheck->isChecked())
                    {
                        shakeWidget(ui->serverCheck);
                        return showError("未开启网络服务", "不可使用" + name);
                    }
                    QDesktopServices::openUrl(getDomainPort() + urlR);
                });

                auto btn = new WaterCircleButton(QIcon(":/icons/copy"), widget);
                btnHLayout->addWidget(btn);
                btn->setFixedSize(btnSize, btnSize);
                btn->setSquareSize();
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFixedForePos();
                btn->setToolTip("复制URL，可粘贴到直播姬/OBS的“浏览器”中");
                // btn->hide();
                // connect(widget, SIGNAL(signalMouseEnter()), btn, SLOT(show()));
                // connect(widget, SIGNAL(signalMouseLeave()), btn, SLOT(hide()));
                connect(btn, &InteractiveButtonBase::clicked, this, [=]{
                    QApplication::clipboard()->setText(getDomainPort() + urlR);
                });
            }

            // 配置
            if (!stsR.isEmpty())
            {
                auto btn = new WaterCircleButton(QIcon(":/icons/generate"), widget);
                btnHLayout->addWidget(btn);
                btn->setFixedSize(btnSize, btnSize);
                btn->setSquareSize();
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFixedForePos();
                btn->setToolTip("打开扩展的基础配置\n更深层次的设置需要修改源代码");
                // btn->hide();
                // connect(widget, SIGNAL(signalMouseEnter()), btn, SLOT(show()));
                // connect(widget, SIGNAL(signalMouseLeave()), btn, SLOT(hide()));
                connect(btn, &InteractiveButtonBase::clicked, this, [=]{
                    QDesktopServices::openUrl(getDomainPort() + stsR);
                });
            }

            // 导入代码
            if (inf.contains("code") && code.isArray())
            {
                auto btn = new WaterCircleButton(QIcon(":/icons/code"), widget);
                btnHLayout->addWidget(btn);
                btn->setFixedSize(btnSize, btnSize);
                btn->setSquareSize();
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFixedForePos();
                btn->setToolTip("一键导入要用到的代码\n如果已经存在，可能会重复导入");
                // btn->hide();
                // connect(widget, SIGNAL(signalMouseEnter()), btn, SLOT(show()));
                // connect(widget, SIGNAL(signalMouseLeave()), btn, SLOT(hide()));
                connect(btn, &InteractiveButtonBase::clicked, this, [=]{
                    qInfo() << "导入网页小程序代码";
                    QApplication::clipboard()->setText(QJsonDocument(code.toArray()).toJson());
                    on_actionPaste_Code_triggered();
                });
            }

            // 编辑CSS
            if (!cssR.isEmpty())
            {
                if (cssR.startsWith("/"))
                    cssR = cssR.right(cssR.length() - 1);
                if (cssC.startsWith("/"))
                    cssC = cssC.right(cssC.length() - 1);
                auto btn = new WaterCircleButton(QIcon(":/icons/modify"), widget);
                btnHLayout->addWidget(btn);
                btn->setFixedSize(btnSize, btnSize);
                btn->setSquareSize();
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFixedForePos();
                btn->setToolTip("修改或者导入CSS样式");
                // btn->hide();
                // connect(widget, SIGNAL(signalMouseEnter()), btn, SLOT(show()));
                // connect(widget, SIGNAL(signalMouseLeave()), btn, SLOT(hide()));
                connect(btn, &InteractiveButtonBase::clicked, this, [=]{
                    QString path = wwwDir.absoluteFilePath(cssR);
                    QString pathC = wwwDir.absoluteFilePath(cssC);
                    qInfo() << "编辑页面：" << path;
                    QString content;
                    if (isFileExist(pathC))
                        content = readTextFile(pathC);
                    else
                        content = readTextFileIfExist(path);
                    bool ok;
                    content = QssEditDialog::getText(this, "设置样式：" + name, "支持CSS样式，修改后刷新页面生效", content, &ok);
                    if (!ok)
                        return ;
                    writeTextFile(pathC, content);
                });
            }

            // 打开目录
            if (!dirR.isEmpty())
            {
                if (dirR.startsWith("/"))
                    dirR = dirR.right(dirR.length() - 1);
                auto btn = new WaterCircleButton(QIcon(":/icons/dir"), widget);
                btnHLayout->addWidget(btn);
                btn->setFixedSize(btnSize, btnSize);
                btn->setSquareSize();
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFixedForePos();
                btn->setToolTip("打开扩展指定目录，修改相关资源");
                // btn->hide();
                // connect(widget, SIGNAL(signalMouseEnter()), btn, SLOT(show()));
                // connect(widget, SIGNAL(signalMouseLeave()), btn, SLOT(hide()));
                connect(btn, &InteractiveButtonBase::clicked, this, [=]{
                    QString path = wwwDir.absoluteFilePath(dirR);
                    QDesktopServices::openUrl(QUrl(path));
                });
            }

            // 打开文件
            if (!fileR.isEmpty())
            {
                if (fileR.startsWith("/"))
                    fileR = fileR.right(fileR.length() - 1);
                auto btn = new WaterCircleButton(QIcon(":/icons/file"), widget);
                btnHLayout->addWidget(btn);
                btn->setFixedSize(btnSize, btnSize);
                btn->setSquareSize();
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFixedForePos();
                btn->setToolTip("打开扩展指定文件，如抽奖结果");
                // btn->hide();
                // connect(widget, SIGNAL(signalMouseEnter()), btn, SLOT(show()));
                // connect(widget, SIGNAL(signalMouseLeave()), btn, SLOT(hide()));
                connect(btn, &InteractiveButtonBase::clicked, this, [=]{
                    QString path = wwwDir.absoluteFilePath(fileR);
                    QDesktopServices::openUrl(QUrl(path));
                });
            }

            btnHLayout->addStretch(1);

            auto item = new QListWidgetItem(ui->extensionListWidget);
            ui->extensionListWidget->setItemWidget(item, widget);
            widget->adjustSize();
            item->setSizeHint(widget->size());
        });
    }
}

void MainWindow::shakeWidget(QWidget *widget)
{
    static QList<QWidget*> shakingWidgets;

    if (shakingWidgets.contains(widget))
        return ;

    int nX = widget->x();
    int nY = widget->y();
    QPropertyAnimation *ani = new QPropertyAnimation(widget, "geometry");
    ani->setEasingCurve(QEasingCurve::InOutSine);
    ani->setDuration(500);
    ani->setStartValue(QRect(QPoint(nX,nY), widget->size()));

    int range = 5; // 抖动距离
    int nShakeCount = 20; //抖动次数
    double nStep = 1.0 / nShakeCount;
    for(int i = 1; i < nShakeCount; i++){
        range = i & 1 ? -range : range;
        ani->setKeyValueAt(nStep * i, QRect(QPoint(nX + range, nY), widget->size()));
    }

    ani->setEndValue(QRect(QPoint(nX,nY), widget->size()));
    ani->start(QAbstractAnimation::DeleteWhenStopped);

    shakingWidgets.append(widget);
    connect(ani, &QPropertyAnimation::stateChanged, this, [=](QAbstractAnimation::State state){
        if (state == QPropertyAnimation::State::Stopped)
        {
            shakingWidgets.removeOne(widget);
        }
    });
}

void MainWindow::saveGameNumbers(int channel)
{
    auto list = gameNumberLists[channel];
    QStringList sl;
    foreach (qint64 val, list)
        sl << snum(val);
    heaps->setValue("game_numbers/r" + snum(channel), sl.join(";"));
}

void MainWindow::restoreGameNumbers()
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (!heaps->contains("game_numbers/r" + snum(i)))
            continue;

        QStringList sl = heaps->value("game_numbers/r" + snum(i)).toString().split(";");
        auto& list = gameNumberLists[i];
        foreach (QString s, sl)
            list << s.toLongLong();
    }
}

void MainWindow::saveGameTexts(int channel)
{
    auto list = gameTextLists[channel];
    heaps->setValue("game_texts/r" + snum(channel), list.join(MAGICAL_SPLIT_CHAR));
}

void MainWindow::restoreGameTexts()
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (!heaps->contains("game_texts/r" + snum(i)))
            continue;

        QStringList sl = heaps->value("game_texts/r" + snum(i)).toString().split(MAGICAL_SPLIT_CHAR);
        gameTextLists[i] = sl;
    }
}

void MainWindow::setUrlCookie(const QString &url, QNetworkRequest *request)
{
    if (url.contains("bilibili.com") && !ac->browserCookie.isEmpty())
        request->setHeader(QNetworkRequest::CookieHeader, ac->userCookies);
    if (url.contains("cloud.baidu.com"))
        request->setRawHeader("Referer", "https://ai.baidu.com/tech/speech/tts_online");
}

void MainWindow::openLink(QString link)
{
    if (!link.startsWith("this://"))
    {
        QDesktopServices::openUrl(link);
        return ;
    }

    link = link.right(link.length() - 7);

    if (link == "buy_vip") // 购买VIP
    {
        on_actionBuy_VIP_triggered();
    }
    else if (link == "capture_manager") // 截图管理
    {
        on_actionPicture_Browser_triggered();
    }
    else if (link == "start_pk") // 开始大乱斗
    {
        on_actionJoin_Battle_triggered();
    }
    else if (link == "guard_catch") // 黑听舰长捕捉
    {
        on_actionGuard_Online_triggered();
    }
    else if (link == "catch_you") // 跑骚捕捉
    {
        on_actionCatch_You_Online_triggered();
    }
    else if (link == "live_status") // 直播间状态查询
    {
        on_actionRoom_Status_triggered();
    }
    else if (link == "lyrics") // 发送歌词
    {
        on_actionCreate_Video_LRC_triggered();
    }
    else if (link == "video_stream_url") // 拷贝视频流地址
    {
        on_actionGet_Play_Url_triggered();
    }
    else if (link == "donation") // 捐赠
    {
        on_actionSponsor_triggered();
    }
    else if (link == "copy_qq") // 复制QQ群
    {
        QApplication::clipboard()->setText("1038738410");
    }
    else if (link == "live_danmaku")
    {
        on_actionShow_Live_Danmaku_triggered();
    }
    else if (link == "live_video")
    {
        on_actionShow_Live_Video_triggered();
    }
    else if (link == "pk_live_video")
    {
        on_actionShow_PK_Video_triggered();
    }
    else if (link == "music")
    {
        on_actionShow_Order_Player_Window_triggered();
    }
    else if (link == "send_long_text")
    {
        on_actionSend_Long_Text_triggered();
    }
    else if (link == "qq_qrcode")
    {
        // 直接把下面的图片设置成这个二维码的
        ui->label_39->setStyleSheet("border-image:url(:/documents/qq_qrcode);\
                                    background-position:center;\
                                    background-repeat:none;");
    }
    else if (link == "gift_list")
    {
        on_actionShow_Gift_List_triggered();
    }
}

/// 显示礼物在界面上
/// 只显示超过一定金额的礼物
void MainWindow::addGuiGiftList(const LiveDanmaku &danmaku)
{
    if (us->closeGui)
        return ;

    // 设置下限
    if (!danmaku.isGoldCoin() || danmaku.getTotalCoin() < 1000) // 超过1元才算
        return ;

    // 测试代码：
    // addGuiGiftList(LiveDanmaku("测试用户", 30607, "测试心心", qrand() % 20, 123456, QDateTime::currentDateTime(), "gold", 10000));

    auto addToList = [=](const QPixmap& pixmap){
        // 创建控件
        InteractiveButtonBase* card = new InteractiveButtonBase(this);
        QVBoxLayout* layout = new QVBoxLayout(card);
        QLabel* imgLabel = new QLabel(card);
        QLabel* giftNameLabel = new QLabel(danmaku.getGiftName(), card);
        QLabel* userNameLabel = new QLabel(danmaku.getNickname(), card);
        layout->addWidget(imgLabel);
        layout->addWidget(giftNameLabel);
        layout->addWidget(userNameLabel);
        card->setLayout(layout);
        card->show();

        layout->setSpacing(1);
        layout->setMargin(3);
        card->setObjectName("giftCard");
        imgLabel->setFixedHeight(rt->giftImgSize);
        imgLabel->setAlignment(Qt::AlignCenter);
        giftNameLabel->setAlignment(Qt::AlignCenter);
        userNameLabel->setAlignment(Qt::AlignCenter);
        if (danmaku.getNumber() > 1)
        {
            giftNameLabel->setText(danmaku.getGiftName() + "×" + snum(danmaku.getNumber()));
            giftNameLabel->adjustSize();
        }
        card->setToolTip("价值：" + snum(danmaku.getTotalCoin() / 1000) + "元");

        // 获取图片
        imgLabel->setPixmap(pixmap.scaled(rt->giftImgSize, rt->giftImgSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

        // 添加到列表
        auto item = new QListWidgetItem();
        ui->giftListWidget->insertItem(0, item);
        ui->giftListWidget->setItemWidget(item, card);

        // 设置样式
        /* card->setStyleSheet("#giftCard { background: transparent;"
                            "border: 0px solid lightgray; "
                            "border-radius: " + snum(rt->fluentRadius) + "px; }"
                            "#giftCard:hover { border: 1px solid lightgray; }"); */
        giftNameLabel->setStyleSheet("font-size: 14px;");
        userNameLabel->setStyleSheet("color: gray;");

        // layout->activate();
        card->setRadius(rt->fluentRadius);
        // card->setFixedSize(card->sizeHint());
        imgLabel->adjustSize();
        giftNameLabel->adjustSize();
        userNameLabel->adjustSize();
        int height = imgLabel->height() + giftNameLabel->height() + userNameLabel->height() + layout->spacing() * 2 + layout->margin() * 2;
        card->setFixedSize(imgLabel->width() + layout->margin() * 2, height);
        item->setSizeHint(card->size());
        if (giftNameLabel->sizeHint().width() > imgLabel->width())
            giftNameLabel->setAlignment(Qt::AlignLeft);
        if (userNameLabel->sizeHint().width() > imgLabel->width())
            userNameLabel->setAlignment(Qt::AlignLeft);
        if (ui->giftListWidget->height() < card->height())
            ui->giftListWidget->setFixedHeight(card->height());
    };

    // 移除过多的
    int giftListMax = 100; // 最大保留几个（过多可能会占内存）
    if (ui->giftListWidget->count() >= giftListMax)
    {
        auto item = ui->giftListWidget->item(ui->giftListWidget->count() - 1);
        auto widget = ui->giftListWidget->itemWidget(item);
        widget->deleteLater();
        ui->giftListWidget->takeItem(ui->giftListWidget->count() - 1);
    }

    // 本地已有，直接设置
    qint64 id = danmaku.getGiftId();
    if (pl->giftPixmaps.contains(id))
    {
        addToList(pl->giftPixmaps[id]);
        return ;
    }

    // 先下载图片，然后进行设置
    // TODO: 这个URL的图片有些是不准确的
    QString url = "http://openapi.zbmate.com/gift_icons/b/";
    if (danmaku.isGuard()) // 舰长礼物的话，是静态的图片
    {
        int guard = danmaku.getGuard();
        QString name = "jz";
        if (guard == 2)
            name = "td";
        else if (guard == 1)
            name = "zd";
        url += name + ".png";
    }
    else
    {
        if (pl->allGiftMap.contains(id))
            url = pl->allGiftMap.value(id).extraJson.value("img_basic").toString();
        else
            url += snum(id) + ".gif";
    }
    get(url, [=](QNetworkReply* reply){
        QByteArray jpegData = reply->readAll();
        QPixmap pixmap;
        pixmap.loadFromData(jpegData);
        if (pixmap.isNull())
        {
            showError("获取礼物图片出错", danmaku.getGiftName() + " " + snum(danmaku.getGiftId()));
            qWarning() << "礼物地址：" << url;
            return ;
        }

        pl->giftPixmaps[id] = pixmap;
        addToList(pixmap);
    });
}

QString MainWindow::GetFileVertion(QString fullName)
{
#if defined(Q_OS_WIN)
    DWORD dwLen = 0;
    char* lpData=NULL;
    LPCWSTR  str_path;
    str_path=fullName.toStdWString().c_str();
    BOOL bSuccess = FALSE;
    QString fileInfomation;
    DWORD vHandle=0;
    //获得文件基础信息
    //--------------------------------------------------------
    dwLen = GetFileVersionInfoSize( str_path, &vHandle);
    if (0 == dwLen)
    {
        qCritical()<<"获取版本字节信息失败!";
        return "";
    }

    lpData =(char*)malloc(dwLen+1);
    if (NULL == lpData)
    {
        qCritical()<<"分配内存失败";
        return "";
    }
    bSuccess = GetFileVersionInfo( fullName.toStdWString().c_str(),0, dwLen+1, lpData);
    if (!bSuccess)
    {
        qCritical()<<"获取文件版本信息错误!";
        return "";
    }
    LPVOID lpBuffer = NULL;
    UINT uLen = 0;

    //获得语言和代码页(language and code page),规定，套用即可
    //---------------------------------------------------
    bSuccess = VerQueryValue( lpData,
                              (TEXT("\\VarFileInfo\\Translation")),
                              &lpBuffer,
                              &uLen);
    QString strTranslation,str1,str2;
    unsigned short int *p =(unsigned short int *)lpBuffer;
    str1.setNum(*p,16);
    str1="000"+ str1;
    strTranslation+= str1.mid(str1.size()-4,4);
    str2.setNum(*(++p),16);
    str2="000"+ str2;
    strTranslation+= str2.mid(str2.size()-4,4);

    QString str_value;
    QString code;
    //以上步骤需按序进行，以下步骤可根据需要增删或者调整

    //获得产品版本信息：ProductVersion
    code ="\\StringFileInfo\\"+ strTranslation +"\\ProductVersion";
    bSuccess = VerQueryValue(lpData,
                             (code.toStdWString().c_str()),
                             &lpBuffer,
                             &uLen);
    if (!bSuccess)
    {
        qCritical()<<"获取产品版本信息错误!";
    }
    else
    {
        str_value=QString::fromUtf16((const unsigned short int *)lpBuffer)+"\n";
    }
    return str_value;
#else
    QString text = readTextFileAutoCodec(":/documents/resource.rc");
    QString key = "VALUE \"ProductVersion\",";
    int pos = text.indexOf(key);
    if (pos != -1)
    {
        int right = text.indexOf("\n", pos);
        if (right == -1)
            right = text.length();
        QString version = text.mid(pos + key.length(), right - pos - key.length()).trimmed();
        if (version.startsWith("\"") && version.endsWith("\""))
            version = version.mid(1, version.length() - 2);
        return version;
    }
    return "";
#endif
}

/**
 * 升级版本所所需要修改的数据
 */
void MainWindow::upgradeVersionToLastest(QString oldVersion)
{
    if (oldVersion.startsWith("v") || oldVersion.startsWith("V"))
        oldVersion.replace(0, 1, "");
    QStringList versions = {
        "3.6.3",
        "4.6.0",
        "4.8.2",
        rt->appVersion // 最后一个一定是最新版本
    };
    int index = 0;
    while (index < versions.size())
    {
        if (versions.at(index) < oldVersion)
            index++;
        else
            break;
    }
    for (int i = index; i < versions.size() - 1; i++)
    {
        QString ver = versions.at(i);
        qInfo() << "数据升级：" << ver << " -> " << versions.at(i+1);
        upgradeOneVersionData(ver);
        sqlService.upgradeDb(versions.at(i+1));
    }
}

void MainWindow::upgradeOneVersionData(QString beforeVersion)
{
    if (beforeVersion == "3.6.3")
    {
        us->beginGroup("heaps");
        heaps->beginGroup("heaps");
        auto keys = us->allKeys();
        for (int i = 0; i < keys.size(); i++)
        {
            QString key = keys.at(i);
            heaps->setValue(key, us->value(key));
            us->remove(key);
        }
        heaps->endGroup();
        us->endGroup();
    }
    else if (beforeVersion == "4.6.0")
    {
        us->setValue("runtime/first_use_time", QDateTime::currentSecsSinceEpoch());
    }
    else if (beforeVersion == "4.8.2")
    {
        deleteDir(rt->dataPath + "musics");
    }
}

bool MainWindow::hasInstallVC2015()
{
#ifdef Q_OS_WINDOWS
    QString header = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
    QSettings reg(header,QSettings::NativeFormat);
    QMap<QString,QString> m_data;
    QStringList sum = reg.allKeys();
    for(int m  = 0 ; m < sum.size();++m){
        QString id = sum.at(m);
        int end = id.indexOf("}");
        if(end > 0){
            id = id.mid(0,end+1);

            if(!m_data.keys().contains(id)){
                QSettings gt(header + id,QSettings::NativeFormat);
                QString name = gt.value("DisplayName").toString();
                if(!name.isEmpty() && name.contains("Microsoft Visual C++")){
                    m_data[id] = name;
                }

            }

        }
    }

    QMap<QString,QString>::const_iterator it = m_data.constBegin();
    while (it != m_data.constEnd()) {
        if(it.value().contains(QRegExp("Microsoft Visual C\\+\\+ 20(1[56789]|[2-9])")))
            return true;
        ++it;

    }
#endif
    return false;
}

void MainWindow::generateDefaultCode(QString path)
{
    MyJson json;
    json.insert("welcome", ui->autoWelcomeWordsEdit->toPlainText());
    json.insert("gift", ui->autoThankWordsEdit->toPlainText());
    json.insert("attention", ui->autoAttentionWordsEdit->toPlainText());

    QJsonArray array;
    for (int row = 0; row < ui->taskListWidget->count(); row++)
        array.append(static_cast<TaskWidget*>(ui->taskListWidget->itemWidget(ui->taskListWidget->item(row)))->toJson());
    json.insert("timer_task", array);

    array = QJsonArray();
    for (int row = 0; row < ui->replyListWidget->count(); row++)
        array.append(static_cast<ReplyWidget*>(ui->replyListWidget->itemWidget(ui->replyListWidget->item(row)))->toJson());
    json.insert("auto_reply", array);

    array = QJsonArray();
    for (int row = 0; row < ui->eventListWidget->count(); row++)
        array.append(static_cast<EventWidget*>(ui->eventListWidget->itemWidget(ui->eventListWidget->item(row)))->toJson());
    json.insert("event_action", array);

    json.insert("block_keys", ui->autoBlockNewbieKeysEdit->toPlainText());
    json.insert("block_notify", ui->autoBlockNewbieNotifyWordsEdit->toPlainText());
    json.insert("block_tip", ui->promptBlockNewbieKeysEdit->toPlainText());

    if (path.isEmpty())
        path = QApplication::applicationDirPath() + "/default_code.json";
    writeTextFile(path, QString::fromUtf8(json.toBa()));
}

void MainWindow::readDefaultCode(QString path)
{
    if (path.isEmpty())
    {
        path = QApplication::applicationDirPath() + "/default_code.json";
        if (!QFileInfo(path).exists())
            path = ":/documents/default_code";
    }

    QString text = readTextFileIfExist(path);
    MyJson json(text.toUtf8());
    if (json.contains("welcome"))
    {
        ui->autoWelcomeWordsEdit->setPlainText(json.s("welcome"));
    }
    if (json.contains("gift"))
    {
        ui->autoThankWordsEdit->setPlainText(json.s("gift"));
    }
    if (json.contains("attention"))
    {
        ui->autoAttentionWordsEdit->setPlainText(json.s("attention"));
    }
    if (json.contains("timer_task"))
    {
        json.each("timer_task", [=](MyJson obj){
            addTimerTask(obj);
        });
        saveTaskList();
    }
    if (json.contains("auto_reply"))
    {
        json.each("auto_reply", [=](MyJson obj){
            addAutoReply(obj);
        });
        saveReplyList();
    }
    if (json.contains("event_action"))
    {
        json.each("event_action", [=](MyJson obj){
            addEventAction(obj);
        });
        saveEventList();
    }
    if (json.contains("block_keys"))
    {
        ui->autoBlockNewbieKeysEdit->setPlainText(json.s("block_keys"));
    }
    if (json.contains("block_notify"))
    {
        ui->autoBlockNewbieNotifyWordsEdit->setPlainText(json.s("block_notify"));
    }
    if (json.contains("block_tip"))
    {
        ui->promptBlockNewbieKeysEdit->setPlainText(json.s("block_tip"));
    }
}

void MainWindow::showError(QString title, QString s) const
{
    statusLabel->setText(title + ": " + s);
    qWarning() << title << ":" << s;
    tip_box->createTipCard(new NotificationEntry("", title, s));
}

void MainWindow::showError(QString s) const
{
    statusLabel->setText(s);
    qWarning() << s;
    tip_box->createTipCard(new NotificationEntry("", "", s));
}

void MainWindow::showNotify(QString title, QString s) const
{
    statusLabel->setText(title + ": " + s);
    qInfo() << title << ":" << s;
    tip_box->createTipCard(new NotificationEntry("", title, s));
}

void MainWindow::showNotify(QString s) const
{
    statusLabel->setText(s);
    qInfo() << s;
    tip_box->createTipCard(new NotificationEntry("", "", s));
}

/**
 * 用户设定的文件名
 * 获取相应的文件绝对路径
 */
QString MainWindow::toFilePath(const QString &fileName) const
{
    if (QFileInfo(fileName).isRelative())
        return rt->dataPath + fileName;
    return fileName;
}

/**
 * 从不安全的输入方式读取到的文本，如读取txt
 * 转换为代码中可以解析的安全的文本
 */
QString MainWindow::toSingleLine(QString text) const
{
    return text.replace("\n", "%n%").replace("\\n", "%m%");
}

/**
 * 从代码中解析到的文本，变为可以保存的文本
 */
QString MainWindow::toMultiLine(QString text) const
{
    return text.replace("%n%", "\n").replace("%m%", "\\n");
}

QString MainWindow::toRunableCode(QString text) const
{
    return text.replace("\\%", "%");
}

void MainWindow::initLiveOpenService()
{
    if (liveOpenService)
        return ;

    // 初始化
    liveOpenService = new LiveOpenService(this);
}

void MainWindow::initDbService()
{
    // TODO: 可更换路径
    sqlService.setDbPath(rt->dataPath + "database.db");
    if (saveToSqlite)
        sqlService.open();

    connect(this, &MainWindow::signalNewDanmaku, this, [=](const LiveDanmaku &danmaku){
        if (danmaku.isPkLink()) // 不包含PK同步的弹幕
            return ;
        if (danmaku.isNoReply() && !danmaku.isAutoSend()) // 不包含自动发送的弹幕
            return ;
        if (saveToSqlite)
            sqlService.insertDanmaku(danmaku);
    });
    connect(&sqlService, &SqlService::signalError, this, [=](const QString& err) {
        showError("数据库", err);
    });
}

void MainWindow::showSqlQueryResult(QString sql)
{
    DBBrowser* dbb = new DBBrowser(&sqlService, us, nullptr);
    dbb->setGeometry(this->geometry());
    connect(dbb, &DBBrowser::signalProcessVariant, this, [=](QString& code) {
        code = processDanmakuVariants(code, LiveDanmaku());
    });
    if (!sql.isEmpty())
    {
        dbb->showQueryResult(sql);
    }
    dbb->show();
}

void MainWindow::on_actionMany_Robots_triggered()
{
    if (!hostList.size()) // 未连接
        return ;

    HostInfo hostServer = hostList.at(0);
    QString host = QString("wss://%1:%2/sub").arg(hostServer.host).arg(hostServer.wss_port);

    QSslConfiguration config = this->socket->sslConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    config.setProtocol(QSsl::TlsV1SslV3);

    for (int i = 0; i < 1000; i++)
    {
        QWebSocket* socket = new QWebSocket();
        connect(socket, &QWebSocket::connected, this, [=]{
            qInfo() << "rSocket" << i << "connected";
            // 5秒内发送认证包
            sendVeriPacket(socket, pkRoomId, pkToken);
        });
        connect(socket, &QWebSocket::disconnected, this, [=]{
            robots_sockets.removeOne(socket);
            socket->deleteLater();
        });
        socket->setSslConfiguration(config);
        socket->open(host);
        robots_sockets.append(socket);
    }
}

void MainWindow::on_judgeRobotCheck_clicked()
{
    judgeRobot = (judgeRobot + 1) % 3;
    if (judgeRobot == 0)
        ui->judgeRobotCheck->setCheckState(Qt::Unchecked);
    else if (judgeRobot == 1)
        ui->judgeRobotCheck->setCheckState(Qt::PartiallyChecked);
    else if (judgeRobot == 2)
        ui->judgeRobotCheck->setCheckState(Qt::Checked);
    ui->judgeRobotCheck->setText(judgeRobot == 1 ? "机器人判断(仅关注)" : "机器人判断");

    us->setValue("danmaku/judgeRobot", judgeRobot);
}

void MainWindow::on_actionAdd_Room_To_List_triggered()
{
    if (ac->roomId.isEmpty())
        return ;

    QStringList list = us->value("custom/rooms", "").toString().split(";", QString::SkipEmptyParts);
    for (int i = 0; i < list.size(); i++)
    {
        QStringList texts = list.at(i).split(",", QString::SkipEmptyParts);
        if (texts.size() < 1)
            continue ;
        QString id = texts.first();
        QString name = texts.size() >= 2 ? texts.at(1) : id;
        if (id == ac->roomId) // 找到这个
        {
            ui->menu_3->removeAction(ui->menu_3->actions().at(ui->menu_3->actions().size() - (list.size() - i)));
            list.removeAt(i);
            us->setValue("custom/rooms", list.join(";"));
            return ;
        }
    }

    QString id = ac->roomId; // 不能用成员变量，否则无效（lambda中值会变，自己试试就知道了）
    QString name = ac->upName;

    list.append(id + "," + name.replace(";","").replace(",",""));
    us->setValue("custom/rooms", list.join(";"));

    QAction* action = new QAction(name, this);
    ui->menu_3->addAction(action);
    connect(action, &QAction::triggered, this, [=]{
        ui->roomIdEdit->setText(id);
        on_roomIdEdit_editingFinished();
    });
}

void MainWindow::on_recordCheck_clicked()
{
    bool check = ui->recordCheck->isChecked();
    us->setValue("record/enabled", check);

    if (check)
    {
        if (!ac->roomId.isEmpty() && isLiving())
            startLiveRecord();
    }
    else
        finishLiveRecord();
}

void MainWindow::on_recordSplitSpin_valueChanged(int arg1)
{
    us->setValue("record/split", arg1);
    if (recordTimer)
        recordTimer->setInterval(arg1 * 60000);
}

void MainWindow::on_sendWelcomeTextCheck_clicked()
{
    us->setValue("danmaku/sendWelcomeText", ui->sendWelcomeTextCheck->isChecked());
}

void MainWindow::on_sendWelcomeVoiceCheck_clicked()
{
    us->setValue("danmaku/sendWelcomeVoice", ui->sendWelcomeVoiceCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->sendWelcomeVoiceCheck->isChecked())
        initTTS();
#endif
}

void MainWindow::on_sendGiftTextCheck_clicked()
{
    us->setValue("danmaku/sendGiftText", ui->sendGiftTextCheck->isChecked());
}

void MainWindow::on_sendGiftVoiceCheck_clicked()
{
    us->setValue("danmaku/sendGiftVoice", ui->sendGiftVoiceCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->sendGiftVoiceCheck->isChecked())
        initTTS();
#endif
}

void MainWindow::on_sendAttentionTextCheck_clicked()
{
    us->setValue("danmaku/sendAttentionText", ui->sendAttentionTextCheck->isChecked());
}

void MainWindow::on_sendAttentionVoiceCheck_clicked()
{
    us->setValue("danmaku/sendAttentionVoice", ui->sendAttentionVoiceCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->sendAttentionVoiceCheck->isChecked())
        initTTS();
#endif
}

void MainWindow::on_enableScreenDanmakuCheck_clicked()
{
    us->setValue("screendanmaku/enableDanmaku", ui->enableScreenDanmakuCheck->isChecked());
    ui->enableScreenMsgCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
    ui->screenDanmakuWithNameCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
}

void MainWindow::on_enableScreenMsgCheck_clicked()
{
    us->setValue("screendanmaku/enableMsg", ui->enableScreenMsgCheck->isChecked());
}

void MainWindow::on_screenDanmakuWithNameCheck_clicked()
{
    us->setValue("screendanmaku/showName", ui->screenDanmakuWithNameCheck->isChecked());
}

void MainWindow::on_screenDanmakuLeftSpin_valueChanged(int arg1)
{
    us->setValue("screendanmaku/left", arg1);
}

void MainWindow::on_screenDanmakuRightSpin_valueChanged(int arg1)
{
    us->setValue("screendanmaku/right", arg1);
}

void MainWindow::on_screenDanmakuTopSpin_valueChanged(int arg1)
{
    us->setValue("screendanmaku/top", arg1);
}

void MainWindow::on_screenDanmakuBottomSpin_valueChanged(int arg1)
{
    us->setValue("screendanmaku/bottom", arg1);
}

void MainWindow::on_screenDanmakuSpeedSpin_valueChanged(int arg1)
{
    us->setValue("screendanmaku/speed", arg1);
}

void MainWindow::on_screenDanmakuFontButton_clicked()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok,this);
    if (!ok)
        return ;
    this->screenDanmakuFont = font;
    this->setFont(font);
    us->setValue("screendanmaku/font", screenDanmakuFont.toString());
}

void MainWindow::on_screenDanmakuColorButton_clicked()
{
    QColor c = QColorDialog::getColor(screenDanmakuColor, this, "选择昵称颜色", QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return ;
    if (c != screenDanmakuColor)
    {
        us->setValue("screendanmaku/color", screenDanmakuColor = c);
    }
}

void MainWindow::on_autoSpeekDanmakuCheck_clicked()
{
    us->setValue("danmaku/autoSpeek", ui->autoSpeekDanmakuCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->autoSpeekDanmakuCheck->isChecked())
        initTTS();
#endif
}

void MainWindow::on_diangeFormatEdit_textEdited(const QString &text)
{
    diangeFormatString = text;
    us->setValue("danmaku/diangeFormat", diangeFormatString);
}

void MainWindow::on_diangeNeedMedalCheck_clicked()
{
    us->setValue("danmaku/diangeNeedMedal", ui->diangeNeedMedalCheck->isChecked());
}

void MainWindow::on_showOrderPlayerButton_clicked()
{
    on_actionShow_Order_Player_Window_triggered();
}

void MainWindow::on_diangeShuaCheck_clicked()
{
    us->setValue("danmaku/diangeShua", ui->diangeShuaCheck->isChecked());
}

void MainWindow::on_orderSongShuaSpin_editingFinished()
{
    us->setValue("danmaku/diangeShuaCount", ui->orderSongShuaSpin->value());
}

void MainWindow::on_pkMelonValButton_clicked()
{
    bool ok = false;
    int val = QInputDialog::getInt(this, "乱斗值比例", "1乱斗值等于多少金瓜子？10或100？", goldTransPk, 1, 100, 1, &ok);
    if (!ok)
        return ;
    goldTransPk = val;
    us->setValue("pk/goldTransPk", goldTransPk);
}

void MainWindow::slotPkEnding()
{
    qInfo() << "大乱斗结束前情况：" << myVotes << matchVotes
             << QDateTime::currentSecsSinceEpoch() << pkEndTime;

    pkEnding = true;
    pkVoting = 0;

    // 几个吃瓜就能解决的……
    {
        QString text = QString("大乱斗尾声：%1 vs %2")
                .arg(myVotes).arg(matchVotes);
        if (!pkRoomId.isEmpty())
        {
            int totalCount = us->danmakuCounts->value("pk/" + pkRoomId, 0).toInt() - 1;
            int toutaCount = us->danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
            if (totalCount > 1) // 开始的时候就已经+1了，上面已经-1
                text += QString("  偷塔概率:%1/%2")
                                .arg(toutaCount).arg(totalCount);
        }

        // localNotify(text);
    }
    execTouta();

    triggerCmdEvent("PK_ENDING", LiveDanmaku(), true);
}

/**
 * 机器人开始工作
 * 开播时连接/连接后开播
 * 不包括视频大乱斗的临时开启
 */
void MainWindow::slotStartWork()
{
    // 初始化开播数据
    liveTimestamp = QDateTime::currentMSecsSinceEpoch();

    // 自动更换勋章
    if (ui->autoSwitchMedalCheck->isChecked())
    {
        // switchMedalToRoom(roomId.toLongLong());
        switchMedalToUp(ac->upUid.toLongLong());
    }

    ui->actionShow_Live_Video->setEnabled(true);

    // 赠送过期礼物
    if (ui->sendExpireGiftCheck->isChecked())
    {
        sendExpireGift();
    }

    // 延迟操作（好像是有bug）
    QString ri = ac->roomId;
    QTimer::singleShot(5000, [=]{
        if (ri != ac->roomId)
            return ;

        // 挂小心心
        if (ui->acquireHeartCheck->isChecked() && isLiving())
        {
            sendXliveHeartBeatE();
        }
    });

    // 设置直播状态
    QPixmap face = toLivingPixmap(toCirclePixmap(upFace));
    setWindowIcon(face);
    tray->setIcon(face);

    // 开启弹幕保存（但是之前没有开启，怕有bug）
    if (ui->saveDanmakuToFileCheck->isChecked() && !danmuLogFile)
        startSaveDanmakuToFile();

    // 同步所有的使用房间，避免使用神奇弹幕的偷塔误杀
    QString usedRoom = ac->roomId;
    syncTimer->start((qrand() % 10 + 10) * 1000);

    // 本次直播数据
    liveAllGifts.clear();

    // 获取舰长
    updateExistGuards(0);

    triggerCmdEvent("START_WORK", LiveDanmaku(), true);

    // 云同步
    pullRoomShieldKeyword();

    // 开始工作
    if (ui->liveOpenCheck->isChecked())
    {
        liveOpenService->start();
    }
}

void MainWindow::on_autoSwitchMedalCheck_clicked()
{
    us->setValue("danmaku/autoSwitchMedal", ui->autoSwitchMedalCheck->isChecked());
    if (!ac->roomId.isEmpty() && isLiving())
    {
        // switchMedalToRoom(roomId.toLongLong());
        switchMedalToUp(ac->upUid.toLongLong());
    }
}

void MainWindow::on_sendAutoOnlyLiveCheck_clicked()
{
    us->setValue("danmaku/sendAutoOnlyLive", ui->sendAutoOnlyLiveCheck->isChecked());
}

void MainWindow::on_autoDoSignCheck_clicked()
{
    us->setValue("danmaku/autoDoSign", ui->autoDoSignCheck->isChecked());
}

void MainWindow::on_actionRoom_Status_triggered()
{
    RoomStatusDialog* rsd = new RoomStatusDialog(us, rt->dataPath, nullptr);
    rsd->show();
}

void MainWindow::on_autoLOTCheck_clicked()
{
    us->setValue("danmaku/autoLOT", ui->autoLOTCheck->isChecked());
}

void MainWindow::on_blockNotOnlyNewbieCheck_clicked()
{
    bool enable = ui->blockNotOnlyNewbieCheck->isChecked();
    us->setValue("block/blockNotOnlyNewbieCheck", enable);
}

void MainWindow::on_autoBlockTimeSpin_editingFinished()
{
    us->setValue("block/autoTime", ui->autoBlockTimeSpin->value());
}

/**
 * 所有事件的槽都在这里触发
 */
void MainWindow::triggerCmdEvent(QString cmd, LiveDanmaku danmaku, bool debug)
{
    if (debug)
        qInfo() << "触发事件：" << cmd;
    emit signalCmdEvent(cmd, danmaku);

    sendDanmakuToSockets(cmd, danmaku);
}

void MainWindow::on_voiceLocalRadio_toggled(bool checked)
{
    if (checked)
    {
        voicePlatform = VoiceLocal;
        us->setValue("voice/platform", voicePlatform);
        ui->voiceNameEdit->setText(us->value("voice/localName").toString());

        ui->voiceConfigSettingsCard->show();
        ui->voiceXfySettingsCard->hide();
        ui->voiceMSSettingsCard->hide();
        ui->voiceCustomSettingsCard->hide();
        ui->scrollArea->updateChildWidgets();
        ui->scrollArea->adjustWidgetsPos();
    }
}

void MainWindow::on_voiceXfyRadio_toggled(bool checked)
{
    if (checked)
    {
        voicePlatform = VoiceXfy;
        us->setValue("voice/platform", voicePlatform);
        ui->voiceNameEdit->setText(us->value("xfytts/name", "xiaoyan").toString());

        ui->voiceConfigSettingsCard->show();
        ui->voiceXfySettingsCard->show();
        ui->voiceMSSettingsCard->hide();
        ui->voiceCustomSettingsCard->hide();
        ui->scrollArea->updateChildWidgets();
        ui->scrollArea->adjustWidgetsPos();
    }
}

void MainWindow::on_voiceMSRadio_toggled(bool checked)
{
    if (checked)
    {
        voicePlatform = VoiceMS;
        us->setValue("voice/platform", voicePlatform);
        ui->voiceNameEdit->setText(us->value("xfytts/name", "xiaoyan").toString());

        ui->voiceConfigSettingsCard->hide();
        ui->voiceMSSettingsCard->show();
        ui->voiceXfySettingsCard->hide();
        ui->voiceCustomSettingsCard->hide();
        ui->scrollArea->updateChildWidgets();
        ui->scrollArea->adjustWidgetsPos();
    }
}

void MainWindow::on_voiceCustomRadio_toggled(bool checked)
{
    ui->voiceConfigSettingsCard->setVisible(!checked);
    if (checked)
    {
        voicePlatform = VoiceCustom;
        us->setValue("voice/platform", voicePlatform);
        ui->voiceNameEdit->setText(us->value("voice/customName").toString());

        ui->voiceConfigSettingsCard->show();
        ui->voiceXfySettingsCard->hide();
        ui->voiceMSSettingsCard->hide();
        ui->voiceCustomSettingsCard->show();
        ui->scrollArea->updateChildWidgets();
        ui->scrollArea->adjustWidgetsPos();
    }
}

void MainWindow::on_voiceNameEdit_editingFinished()
{
    voiceName = ui->voiceNameEdit->text();
    qInfo() << "设置发音人：" << voiceName;
    switch (voicePlatform) {
    case VoiceLocal:
        us->setValue("voice/localName", voiceName);
        break;
    case VoiceXfy:
        us->setValue("xfytts/name", voiceName);
        if (xfyTTS)
        {
            xfyTTS->setName(voiceName);
        }
        break;
    case VoiceCustom:
        us->setValue("voice/customName", voiceName);
        break;
    case VoiceMS:
    {
        static bool notified = false;
        if (!notified)
        {
            notified = true;
            showNotify("微软语音", "暂不支持GUI调节");
        }
        break;
    }
    }
}

void MainWindow::on_voiceNameSelectButton_clicked()
{
    if (voicePlatform == VoiceXfy)
    {
        QStringList names{"讯飞小燕<xiaoyan>",
                         "讯飞许久<aisjiuxu>",
                         "讯飞小萍<aisxping>",
                         "讯飞小婧<aisjinger>",
                         "讯飞许小宝<aisbabyxu>"};

        newFacileMenu;
        for (int i = 0; i < names.size(); i++)
        {
            menu->addAction(names.at(i), [=]{
                QString text = names.at(i);
                QRegularExpression re("<(.+)>");
                QRegularExpressionMatch match;
                if (text.indexOf(re, 0, &match) > -1)
                    text = match.capturedTexts().at(1);
                this->ui->voiceNameEdit->setText(text);
                on_voiceNameEdit_editingFinished();
            });
        }
        menu->exec();
    }
}

void MainWindow::on_voicePitchSlider_valueChanged(int value)
{
    us->setValue("voice/pitch", voicePitch = value);
    ui->voicePitchLabel->setText("音调" + snum(value));

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (tts)
        {
            tts->setPitch((voicePitch - 50) / 50.0);
        }
#endif
        break;
    case VoiceXfy:
        if (xfyTTS)
        {
            xfyTTS->setPitch(voicePitch);
        }
        break;
    case VoiceCustom:
        break;
    case VoiceMS:
    {
//        static bool notified = false;
//        if (!notified)
//        {
//            notified = true;
//            showNotify("微软语音", "暂不支持GUI调节");
//        }
        break;
    }
    }
}

void MainWindow::on_voiceSpeedSlider_valueChanged(int value)
{
    us->setValue("voice/speed", voiceSpeed = value);
    ui->voiceSpeedLabel->setText("音速" + snum(value));

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (tts)
        {
            tts->setRate((voiceSpeed - 50) / 50.0);
        }
#endif
        break;
    case VoiceXfy:
        if (xfyTTS)
        {
            xfyTTS->setSpeed(voiceSpeed);
        }
        break;
    case VoiceCustom:
        break;
    case VoiceMS:
    {
//        static bool notified = false;
//        if (!notified)
//        {
//            notified = true;
//            showNotify("微软语音", "暂不支持GUI调节");
//        }
        break;
    }
    }
}

void MainWindow::on_voiceVolumeSlider_valueChanged(int value)
{
    us->setValue("voice/volume", voiceVolume = value);
    ui->voiceVolumeLabel->setText("音量" + snum(value));

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (tts)
        {
            tts->setVolume((voiceVolume) / 100.0);
        }
#endif
        break;
    case VoiceXfy:
        if (xfyTTS)
        {
            xfyTTS->setVolume(voiceVolume);
        }
        break;
    case VoiceCustom:
        break;
    case VoiceMS:
    {
//        static bool notified = false;
//        if (!notified)
//        {
//            notified = true;
//            showNotify("微软语音", "暂不支持GUI调节");
//        }
        break;
    }
    }
}

void MainWindow::on_voicePreviewButton_clicked()
{
    speakText("这是一个语音合成示例");
}

void MainWindow::on_voiceLocalRadio_clicked()
{
#if defined(ENABLE_TEXTTOSPEECH)
    QTimer::singleShot(100, [=]{
        if (!tts)
        {
            initTTS();
        }
        else
        {
            tts->setRate( (voiceSpeed = us->value("voice/speed", 50).toInt() - 50) / 50.0 );
            tts->setPitch( (voicePitch = us->value("voice/pitch", 50).toInt() - 50) / 50.0 );
            tts->setVolume( (voiceVolume = us->value("voice/volume", 50).toInt()) / 100.0 );
        }
    });
#endif
}

void MainWindow::on_voiceXfyRadio_clicked()
{
    QTimer::singleShot(100, [=]{
        if (!xfyTTS)
        {
            initTTS();
        }
        else
        {
            xfyTTS->setName( voiceName = us->value("xfytts/name", "xiaoyan").toString() );
            xfyTTS->setPitch( voicePitch = us->value("voice/pitch", 50).toInt() );
            xfyTTS->setSpeed( voiceSpeed = us->value("voice/speed", 50).toInt() );
            xfyTTS->setVolume( voiceSpeed = us->value("voice/speed", 50).toInt() );
        }
    });
}

void MainWindow::on_voiceMSRadio_clicked()
{
    QTimer::singleShot(100, [=]{
        if (!msTTS)
        {
            initTTS();
        }
        else
        {
            // TODO: 设置音调等内容
            msTTS->clearQueue();
            msTTS->refreshToken();
        }
    });
}

void MainWindow::on_voiceCustomRadio_clicked()
{
    QTimer::singleShot(100, [=]{
        initTTS();
    });
}

void MainWindow::on_label_10_linkActivated(const QString &link)
{
    QDesktopServices::openUrl(link);
}

void MainWindow::on_xfyAppIdEdit_textEdited(const QString &text)
{
    us->setValue("xfytts/appid", text);
    if (xfyTTS)
    {
        xfyTTS->setAppId(text);
    }
}

void MainWindow::on_xfyApiSecretEdit_textEdited(const QString &text)
{
    us->setValue("xfytts/apisecret", text);
    if (xfyTTS)
    {
        xfyTTS->setApiSecret(text);
    }
}

void MainWindow::on_xfyApiKeyEdit_textEdited(const QString &text)
{
    us->setValue("xfytts/apikey", text);
    if (xfyTTS)
    {
        xfyTTS->setApiKey(text);
    }
}

void MainWindow::on_voiceCustomUrlEdit_editingFinished()
{
    us->setValue("voice/customUrl", ui->voiceCustomUrlEdit->text());
}


void MainWindow::on_eternalBlockListButton_clicked()
{
    EternalBlockDialog* dialog = new EternalBlockDialog(&us->eternalBlockUsers, this);
    connect(dialog, SIGNAL(signalCancelEternalBlock(qint64, qint64)), this, SLOT(cancelEternalBlockUser(qint64, qint64)));
    connect(dialog, SIGNAL(signalCancelBlock(qint64, qint64)), this, SLOT(cancelEternalBlockUserAndUnblock(qint64, qint64)));
    dialog->exec();
}

void MainWindow::on_AIReplyMsgCheck_clicked()
{
    Qt::CheckState state = ui->AIReplyMsgCheck->checkState();
    us->setValue("danmaku/aiReplyMsg", state);
    if (state != Qt::PartiallyChecked)
        ui->AIReplyMsgCheck->setText("回复弹幕");
    else
        ui->AIReplyMsgCheck->setText("回复弹幕(仅单条)");
}

void MainWindow::slotAIReplyed(QString reply, qint64 uid)
{
    if (ui->AIReplyMsgCheck->checkState() != Qt::Unchecked)
    {
        // 机器人自己的不回复（不然自己和自己打起来了）
        if (snum(uid) == ac->cookieUid)
            return ;

        // AI回复长度上限，以及过滤
        if (ui->AIReplyMsgCheck->checkState() == Qt::PartiallyChecked
                && reply.length() > danmuLongest)
            return ;

        // 自动断句
        QStringList sl;
        int len = reply.length();
        const int maxOne = danmuLongest;
        int count = (len + maxOne - 1) / maxOne;
        for (int i = 0; i < count; i++)
        {
            sl << reply.mid(i * maxOne, maxOne);
        }
        sendAutoMsg(sl.join("\\n"), LiveDanmaku());
    }
}

void MainWindow::on_danmuLongestSpin_editingFinished()
{
    danmuLongest = ui->danmuLongestSpin->value();
    us->setValue("danmaku/danmuLongest", danmuLongest);
}


void MainWindow::on_startupAnimationCheck_clicked()
{
    us->setValue("mainwindow/splash", ui->startupAnimationCheck->isChecked());
}

void MainWindow::on_serverCheck_clicked()
{
    bool enabled = ui->serverCheck->isChecked();
    us->setValue("server/enabled", enabled);
    if (enabled)
        openServer();
    else
        closeServer();
}

void MainWindow::on_serverPortSpin_editingFinished()
{
    us->setValue("server/port", ui->serverPortSpin->value());
#if defined(ENABLE_HTTP_SERVER)
    if (server)
    {
        closeServer();
        QTimer::singleShot(1000, [=]{
            openServer();
        });
    }
#endif
}

void MainWindow::on_autoPauseOuterMusicCheck_clicked()
{
    us->setValue("danmaku/autoPauseOuterMusic", ui->autoPauseOuterMusicCheck->isChecked());
}

void MainWindow::on_outerMusicKeyEdit_textEdited(const QString &arg1)
{
    us->setValue("danmaku/outerMusicPauseKey", arg1);
}

void MainWindow::on_acquireHeartCheck_clicked()
{
    us->setValue("danmaku/acquireHeart", ui->acquireHeartCheck->isChecked());

    if (ui->acquireHeartCheck->isChecked())
    {
        if (isLiving())
            sendXliveHeartBeatE();
    }
    else
    {
        if (xliveHeartBeatTimer)
            xliveHeartBeatTimer->stop();
    }
}

void MainWindow::on_sendExpireGiftCheck_clicked()
{
    bool enable = ui->sendExpireGiftCheck->isChecked();
    us->setValue("danmaku/sendExpireGift", enable);

    if (enable)
    {
        sendExpireGift();
    }
}

void MainWindow::on_actionPicture_Browser_triggered()
{
    if (!pictureBrowser)
    {
        pictureBrowser = new PictureBrowser(us, nullptr);
    }
    pictureBrowser->show();
    pictureBrowser->readDirectory(rt->dataPath + "captures");
}

void MainWindow::on_orderSongsToFileCheck_clicked()
{
    us->setValue("danmaku/orderSongsToFile", ui->orderSongsToFileCheck->isChecked());

    if (musicWindow && ui->orderSongsToFileCheck->isChecked())
    {
        saveOrderSongs(musicWindow->getOrderSongs());
    }
}

void MainWindow::on_orderSongsToFileFormatEdit_textEdited(const QString &arg1)
{
    us->setValue("danmaku/orderSongsToFileFormat", arg1);

    if (musicWindow && ui->orderSongsToFileCheck->isChecked())
    {
        saveOrderSongs(musicWindow->getOrderSongs());
    }
}

void MainWindow::on_orderSongsToFileMaxSpin_editingFinished()
{
    us->setValue("danmaku/orderSongsToFileMax", ui->orderSongsToFileMaxSpin->value());

    if (musicWindow && ui->orderSongsToFileCheck->isChecked())
    {
        saveOrderSongs(musicWindow->getOrderSongs());
    }
}

void MainWindow::on_playingSongToFileCheck_clicked()
{
    us->setValue("danmaku/playingSongToFile", ui->playingSongToFileCheck->isChecked());

    if (musicWindow && ui->playingSongToFileCheck->isChecked())
    {
        savePlayingSong();
    }
}

void MainWindow::on_playingSongToFileFormatEdit_textEdited(const QString &arg1)
{
    us->setValue("danmaku/playingSongToFileFormat", arg1);

    if (musicWindow && ui->playingSongToFileCheck->isChecked())
    {
        savePlayingSong();
    }
}

void MainWindow::on_actionCatch_You_Online_triggered()
{
    CatchYouWidget* cyw = new CatchYouWidget(us, rt->dataPath, nullptr);
    cyw->show();
    // cyw->catchUser(upUid);
    cyw->setDefaultUser(ac->upUid);
}

void MainWindow::on_pkBlankButton_clicked()
{
    bool ok = false;
    QString text = QInputDialog::getText(this, "自动偷塔黑名单", "设置不启用自动偷塔的房间号列表\n多个房间用任意非数字符号分隔",
                                         QLineEdit::Normal, toutaBlankList.join(";"), &ok);
    if (!ok)
        return ;
    toutaBlankList = text.split(QRegExp("[^\\d]+"), QString::SkipEmptyParts);
    us->setValue("pk/blankList", toutaBlankList.join(";"));
}

void MainWindow::on_actionUpdate_New_Version_triggered()
{
    if (!rt->appDownloadUrl.isEmpty())
    {
        QDesktopServices::openUrl(QUrl(rt->appDownloadUrl));
    }
    else
    {
        syncMagicalRooms();
    }
}

void MainWindow::on_startOnRebootCheck_clicked()
{
    bool enable = ui->startOnRebootCheck->isChecked();
    us->setValue("runtime/startOnReboot", enable);

    QString appName = QApplication::applicationName();
    QString appPath = QDir::toNativeSeparators(QApplication::applicationFilePath());
    QSettings *reg=new QSettings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    QString val = reg->value(appName).toString();// 如果此键不存在，则返回的是空字符串
    if (enable)
        reg->setValue(appName, appPath);// 如果移除的话
    else
        reg->remove(appName);
    reg->deleteLater();
}

void MainWindow::on_domainEdit_editingFinished()
{
    serverDomain = ui->domainEdit->text().trimmed();
    if (serverDomain.isEmpty())
        serverDomain = "localhost";
    us->setValue("server/domain", serverDomain);
}

void MainWindow::prepareQuit()
{
    releaseLiveData();

    QTimer::singleShot(0, [=]{
        if (recordConvertProcess)
        {
            recordConvertProcess->waitForFinished(30000);
            deleteFile(recordLastPath); // 本来是延迟删的，但是怕来不及了
        }

        qApp->quit();
    });
}

void MainWindow::on_giftComboSendCheck_clicked()
{
    us->setValue("danmaku/giftComboSend", ui->giftComboSendCheck->isChecked());
}

void MainWindow::on_giftComboDelaySpin_editingFinished()
{
    us->setValue("danmaku/giftComboDelay", ui->giftComboDelaySpin->value());
}

void MainWindow::on_retryFailedDanmuCheck_clicked()
{
    us->setValue("danmaku/retryFailedDanmu", ui->retryFailedDanmuCheck->isChecked());
}

void MainWindow::on_songLyricsToFileCheck_clicked()
{
    us->setValue("danmaku/songLyricsToFile", ui->songLyricsToFileCheck->isChecked());

    if (musicWindow && ui->songLyricsToFileCheck->isChecked())
    {
        saveSongLyrics();
    }
    if (musicWindow && sendLyricListToSockets)
    {
        sendLyricList();
    }
}

void MainWindow::on_songLyricsToFileMaxSpin_editingFinished()
{
    us->setValue("danmaku/songLyricsToFileMax", ui->songLyricsToFileMaxSpin->value());

    if (musicWindow && ui->songLyricsToFileCheck->isChecked())
    {
        saveSongLyrics();
    }
    if (musicWindow && sendLyricListToSockets)
    {
        sendLyricList();
    }
}

void MainWindow::on_allowWebControlCheck_clicked()
{
    us->setValue("server/allowWebControl", ui->allowWebControlCheck->isChecked());
}

void MainWindow::on_saveEveryGuardCheck_clicked()
{
    us->setValue("danmaku/saveEveryGuard", ui->saveEveryGuardCheck->isChecked());
}

void MainWindow::on_saveMonthGuardCheck_clicked()
{
    us->setValue("danmaku/saveMonthGuard", ui->saveMonthGuardCheck->isChecked());
}

void MainWindow::on_saveEveryGiftCheck_clicked()
{
    us->setValue("danmaku/saveEveryGift", ui->saveEveryGiftCheck->isChecked());
}

void MainWindow::on_exportDailyButton_clicked()
{
    if (ac->roomId.isEmpty())
        return ;

    QString oldPath = us->value("danmaku/exportPath", "").toString();
    QString path = QFileDialog::getSaveFileName(this, "选择导出位置", oldPath, "Tables (*.csv *.txt)");
    if (path.isEmpty())
        return ;
    us->setValue("danmaku/exportPath", path);
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    stream.setGenerateByteOrderMark(true);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    // 拼接数据
    QString dirPath = rt->dataPath + "live_daily";
    QDir dir(dirPath);
    auto files = dir.entryList(QStringList{ac->roomId + "_*.ini"}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    stream << QString("日期,进入人次,进入人数,弹幕数量,新人弹幕,新增关注,关注总数,总金瓜子,总银瓜子,上船人数,船员总数,平均人气,最高人气\n").toUtf8();
    for (int i = 0; i < files.size(); i++)
    {
        QStringList sl;
        QSettings st(dirPath + "/" + files.at(i), QSettings::Format::IniFormat);
        QString day = files.at(i);
        day.replace(ac->roomId + "_", "").replace(".ini", "");
        stream << day << ","
               << st.value("come", 0).toInt() << ","
               << st.value("people_num", 0).toInt() << ","
               << st.value("danmaku", 0).toInt() << ","
               << st.value("newbie_msg", 0).toInt() << ","
               << st.value("new_fans", 0).toInt() << ","
               << st.value("total_fans", 0).toInt() << ","
               << st.value("gift_gold", 0).toInt() << ","
               << st.value("gift_silver", 0).toInt() << ","
               << st.value("guard", 0).toInt() << ","
               << st.value("guard_count", 0).toInt()  << ","
               << st.value("average_popularity", 0).toInt() << ","
               << st.value("max_popularity", 0).toInt()
               << "\n";
    }

    file.close();
}

void MainWindow::on_closeTransMouseButton_clicked()
{
    if (danmakuWindow)
        danmakuWindow->closeTransMouse();
}

void MainWindow::on_pkAutoMaxGoldCheck_clicked()
{
    us->setValue("pk/autoMaxGold", ui->pkAutoMaxGoldCheck->isChecked());
}

void MainWindow::on_saveRecvCmdsCheck_clicked()
{
    saveRecvCmds = ui->saveRecvCmdsCheck->isChecked();
    us->setValue("debug/saveRecvCmds", saveRecvCmds);

    if (saveRecvCmds)
    {
        QDir dir;
        dir.mkdir(rt->dataPath+"websocket_cmds");
        QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss");
        saveCmdsFile = new QFile(rt->dataPath+"websocket_cmds/" + ac->roomId + "_" + date + ".txt");
        saveCmdsFile->open(QIODevice::WriteOnly | QIODevice::Append);
        qInfo() << "开始保存cmds：" << rt->dataPath+"websocket_cmds/" + ac->roomId + "_" + date + ".txt";
    }
    else if (saveCmdsFile)
    {
        qInfo() << "结束保存cmds";
        saveCmdsFile->close();
        saveCmdsFile->deleteLater();
        saveCmdsFile = nullptr;
    }
}

void MainWindow::on_allowRemoteControlCheck_clicked()
{
    us->setValue("danmaku/remoteControl", remoteControl = ui->allowRemoteControlCheck->isChecked());
}

void MainWindow::on_actionJoin_Battle_triggered()
{
    joinBattle(1);
}

void MainWindow::on_actionQRCode_Login_triggered()
{
    QRCodeLoginDialog* dialog = new QRCodeLoginDialog(this);
    connect(dialog, &QRCodeLoginDialog::logined, this, [=](QString s){
        autoSetCookie(s);
    });
    dialog->exec();
}

void MainWindow::on_allowAdminControlCheck_clicked()
{
    us->setValue("danmaku/adminControl", ui->allowAdminControlCheck->isChecked());
}

void MainWindow::on_actionSponsor_triggered()
{
    EscapeDialog* dialog = new EscapeDialog("友情赞助", "您的支持是开发者为爱发电的最大动力！", "不想付钱", "感谢支持", this);
    dialog->exec();
}

void MainWindow::on_actionPaste_Code_triggered()
{
    QString clipText = QApplication::clipboard()->text();
    if (clipText.isEmpty())
    {
        QMessageBox::information(this, "粘贴代码片段", "在定时任务、自动回复、事件动作等列表的右键菜单中复制的结果，可用于备份或发送至其余地方");
        return ;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(clipText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        QMessageBox::information(this, "粘贴代码片段", "只支持JSON格式的代码配置\n" + error.errorString());
        return ;
    }

    addCodeSnippets(doc);
}

void MainWindow::on_actionGenerate_Default_Code_triggered()
{
    QString oldPath = us->value("danmaku/codePath", "").toString();
    QString path = QFileDialog::getSaveFileName(this, "选择导出位置", oldPath, "Json (*.json *.txt)");
    if (path.isEmpty())
        return ;
    us->setValue("danmaku/codePath", path);

    generateDefaultCode(path);
}

void MainWindow::on_actionRead_Default_Code_triggered()
{
    QString oldPath = us->value("danmaku/codePath", "").toString();
    QString path = QFileDialog::getOpenFileName(this, "选择读取位置", oldPath, "Json (*.json *.txt)");
    if (path.isEmpty())
        return ;
    us->setValue("danmaku/codePath", path);

    readDefaultCode(path);
}

void MainWindow::on_giftComboTopCheck_clicked()
{
    us->setValue("danmaku/giftComboTop", ui->giftComboTopCheck->isChecked());
}

void MainWindow::on_giftComboMergeCheck_clicked()
{
    us->setValue("danmaku/giftComboMerge", ui->giftComboMergeCheck->isChecked());
}

void MainWindow::on_listenMedalUpgradeCheck_clicked()
{
    us->setValue("danmaku/listenMedalUpgrade", ui->listenMedalUpgradeCheck->isChecked());
}

void MainWindow::on_pushRecvCmdsButton_clicked()
{
    if (!pushCmdsFile) // 开启模拟输入
    {
        // 输入文件
        QString oldPath = us->value("debug/cmdsPath", "").toString();
        QString path = QFileDialog::getOpenFileName(this, "选择模拟输入的CMDS文件位置", oldPath, "Text (*.txt)");
        if (path.isEmpty())
            return ;
        us->setValue("debug/cmdsPath", path);

        // 创建文件对象
        pushCmdsFile = new QFile(path);
        if (!pushCmdsFile->open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QMessageBox::critical(this, "模拟CMDS", "读取文件失败");
            return ;
        }

        // 本地模式提示
        if (!ui->actionLocal_Mode->isChecked())
        {
            if (QMessageBox::question(this, "模拟CMDS", "您的[本地模式]未开启，可能会回复奇奇怪怪的内容，是否开启[本地模式]？",
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
                ui->actionLocal_Mode->setChecked(localDebug = true);
        }
        ui->saveRecvCmdsCheck->setChecked(saveRecvCmds = false);

        // 定时器
        if (!pushCmdsTimer)
        {
            pushCmdsTimer = new QTimer(this);
            pushCmdsTimer->setInterval(ui->timerPushCmdSpin->value() * 100);
            connect(pushCmdsTimer, SIGNAL(timeout()), this, SLOT(on_pushNextCmdButton_clicked()));
        }
        pushCmdsTimer->setInterval(ui->timerPushCmdSpin->value() * 100);
        if (ui->timerPushCmdCheck->isChecked())
            pushCmdsTimer->start();

        ui->pushRecvCmdsButton->setText("停止");
        ui->pushNextCmdButton->show();
        ui->timerPushCmdCheck->show();
        ui->timerPushCmdSpin->show();
    }
    else // 关闭模拟输入
    {
        if (ui->actionLocal_Mode->isChecked() && us->value("debug/localDebug", false).toBool())
            ui->actionLocal_Mode->setChecked(localDebug = false);

        if (us->value("debug/saveRecvCmds", false).toBool())
            ui->saveRecvCmdsCheck->setChecked(saveRecvCmds = true);

        if (pushCmdsTimer)
            pushCmdsTimer->stop();

        ui->pushRecvCmdsButton->setText("输入CMDS");
        ui->pushNextCmdButton->hide();
        ui->timerPushCmdCheck->hide();
        ui->timerPushCmdSpin->hide();

        pushCmdsFile->close();
        pushCmdsFile->deleteLater();
        pushCmdsFile = nullptr;
    }
}

void MainWindow::on_pushNextCmdButton_clicked()
{
    if (!pushCmdsFile)
        return ;

    QByteArray line = pushCmdsFile->readLine();
    if (line.isNull())
    {
        localNotify("[模拟输入CMDS结束，已退出]");
        on_pushRecvCmdsButton_clicked();
        return ;
    }

    // 处理下一行
    line.replace("__bmd__n__", "\n").replace("__bmd__r__", "\r");
    slotBinaryMessageReceived(line);
}

void MainWindow::on_timerPushCmdCheck_clicked()
{
    bool enable = ui->timerPushCmdCheck->isChecked();
    us->setValue("debug/pushCmdsTimer", enable);

    if (pushCmdsFile && pushCmdsTimer)
    {
        if (enable)
            pushCmdsTimer->start();
        else
            pushCmdsTimer->stop();
    }
}

void MainWindow::on_timerPushCmdSpin_editingFinished()
{
    int val = ui->timerPushCmdSpin->value();
    us->setValue("debug/pushCmdsInterval", val);

    if (pushCmdsTimer)
        pushCmdsTimer->setInterval(val * 100);
}

void MainWindow::on_pkChuanmenCheck_stateChanged(int arg1)
{
    ui->pkMsgSyncCheck->setEnabled(arg1);
}

void MainWindow::on_actionLast_Candidate_triggered()
{
    // QMessageBox::information(this, "最后一次调试的候选弹幕", "-------- 填充变量 --------\n\n" + lastConditionDanmu + "\n\n-------- 随机发送 --------\n\n" + lastCandidateDanmaku);
    QDialog* dialog = new QDialog(this);
    QTabWidget* tabWidget = new QTabWidget(dialog);

    int count = lastConditionDanmu.size();
    for (int i = count - 1; i >= 0; i--)
    {
        QPlainTextEdit* edit = new QPlainTextEdit(tabWidget);
        edit->setPlainText("-------- 填充变量 --------\n\n"
                           + lastConditionDanmu.at(i)
                           + "\n\n-------- 随机发送 --------\n\n"
                           + lastCandidateDanmaku.at(i));
        tabWidget->addTab(edit, i == count - 1 ? "最新" : i == 0 ? "最旧" : snum(i+1));
    }

    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->addWidget(tabWidget);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowTitle("变量历史");
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setGeometry(this->geometry());
    dialog->exec();
}

void MainWindow::on_actionLocal_Mode_triggered()
{
    us->setValue("debug/localDebug", localDebug = ui->actionLocal_Mode->isChecked());
    qInfo() << "本地模式：" << localDebug;
}

void MainWindow::on_actionDebug_Mode_triggered()
{
    us->setValue("debug/debugPrint", debugPrint = ui->actionDebug_Mode->isChecked());
    qInfo() << "调试模式：" << debugPrint;
}

void MainWindow::on_actionGuard_Online_triggered()
{
    GuardOnlineDialog* god = new GuardOnlineDialog(us, ac->roomId, ac->upUid, this);
    god->show();
}

void MainWindow::on_actionOfficial_Website_triggered()
{
    QDesktopServices::openUrl(QUrl("http://danmaku.lyixi.com"));
}

void MainWindow::on_actionAnchor_Case_triggered()
{
    QDesktopServices::openUrl(QUrl("http://anchor.lyixi.com"));
}

void MainWindow::on_robotNameButton_clicked()
{
    newFacileMenu;
    menu->addAction(ui->actionQRCode_Login);
    menu->addAction(ui->actionSet_Cookie);
    menu->addAction(ui->actionSet_Danmaku_Data_Format);
    menu->split()->addAction(ui->actionLogout)->disable(ac->cookieUid.isEmpty());
    menu->exec();
}

void MainWindow::on_thankWelcomeTabButton_clicked()
{
    ui->thankStackedWidget->setCurrentIndex(0);
    us->setValue("mainwindow/thankStackIndex", 0);

    foreach (auto btn, thankTabButtons)
    {
        btn->setNormalColor(Qt::transparent);
        btn->setTextColor(Qt::black);
    }
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setNormalColor(themeSbg);
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setTextColor(themeSfg);
}

void MainWindow::on_thankGiftTabButton_clicked()
{
    ui->thankStackedWidget->setCurrentIndex(1);
    us->setValue("mainwindow/thankStackIndex", 1);

    foreach (auto btn, thankTabButtons)
    {
        btn->setNormalColor(Qt::transparent);
        btn->setTextColor(Qt::black);
    }
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setNormalColor(themeSbg);
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setTextColor(themeSfg);
}

void MainWindow::on_thankAttentionTabButton_clicked()
{
    ui->thankStackedWidget->setCurrentIndex(2);
    us->setValue("mainwindow/thankStackIndex", 2);

    foreach (auto btn, thankTabButtons)
    {
        btn->setNormalColor(Qt::transparent);
        btn->setTextColor(Qt::black);
    }
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setNormalColor(themeSbg);
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setTextColor(themeSfg);
}

void MainWindow::on_blockTabButton_clicked()
{
    ui->thankStackedWidget->setCurrentIndex(3);
    us->setValue("mainwindow/thankStackIndex", 3);

    foreach (auto btn, thankTabButtons)
    {
        btn->setNormalColor(Qt::transparent);
        btn->setTextColor(Qt::black);
    }
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setNormalColor(themeSbg);
    thankTabButtons.at(ui->thankStackedWidget->currentIndex())->setTextColor(themeSfg);
}

void MainWindow::on_sendMsgMoreButton_clicked()
{
    newFacileMenu;

    menu->addAction(ui->actionSend_Long_Text);

    menu->exec();
}

void MainWindow::on_showLiveDanmakuWindowButton_clicked()
{
    on_actionShow_Live_Danmaku_triggered();
}

void MainWindow::on_liveStatusButton_clicked()
{
    if (!ac->liveStatus)
        return ;
    on_actionShow_Live_Video_triggered();
}

void MainWindow::on_tenCardLabel1_linkActivated(const QString &link)
{
    openLink(link);
}

void MainWindow::on_tenCardLabel2_linkActivated(const QString &link)
{
    openLink(link);
}

void MainWindow::on_tenCardLabel3_linkActivated(const QString &link)
{
    openLink(link);
}

void MainWindow::on_tenCardLabel4_linkActivated(const QString &link)
{
    openLink(link);
}

void MainWindow::on_musicBlackListButton_clicked()
{
    QString blackList = orderSongBlackList.join(" ");
    bool ok;
    blackList = TextInputDialog::getText(this, "点歌黑名单", "带有关键词的点歌都将被阻止，并触发“ORDER_SONG_BLOCKED”事件"
                                                        "\n多个关键词用空格隔开，英文字母均使用小写", blackList, &ok);
    if (!ok)
        return ;
    orderSongBlackList = blackList.split(" ", QString::SkipEmptyParts);
    us->setValue("music/blackListKeys", blackList);
}

/// 添加或者删除过滤器
/// 也不一定是过滤器，任何内容都有可能
void MainWindow::setFilter(QString filterName, QString content)
{
    QString filterKey;
    if (filterName == FILTER_MUSIC_ORDER)
    {
        filterKey = "filter/musicOrder";
        filter_musicOrder = content;
        filter_musicOrderRe = QRegularExpression(filter_musicOrder);
    }
    else if (filterName == FILTER_DANMAKU_MSG)
    {
        filterKey = "filter/danmakuMsg";
        filter_danmakuMsg = content;
    }
    else if (filterName == FILTER_DANMAKU_COME)
    {
        filterKey = "filter/danmakuCome";
        filter_danmakuCome = content;
    }
    else if (filterName == FILTER_DANMAKU_GIFT)
    {
        filterKey = "filter/danmakuGift";
        filter_danmakuGift = content;
    }
    else // 不是系统过滤器
        return ;
    us->setValue(filterKey, content);
    qInfo() << "设置过滤器：" << filterKey << content;
}

void MainWindow::on_enableFilterCheck_clicked()
{
    us->setValue("danmaku/enableFilter", enableFilter = ui->enableFilterCheck->isChecked());
}

void MainWindow::on_actionReplace_Variant_triggered()
{
    QString text = saveReplaceVariant();
    bool ok;
    text = TextInputDialog::getText(this, "替换变量", "请输入发送前的替换内容，支持正则表达式：\n示例格式：发送前内容=发送后内容\n仅影响自动发送的弹幕", text, &ok);
    if (!ok)
        return ;
    us->setValue("danmaku/replaceVariant", text);

    restoreReplaceVariant(text);
}

void MainWindow::on_autoClearComeIntervalSpin_editingFinished()
{
    us->setValue("danmaku/clearDidntComeInterval", ui->autoClearComeIntervalSpin->value());
}

void MainWindow::on_roomDescriptionBrowser_anchorClicked(const QUrl &arg1)
{
    QDesktopServices::openUrl(arg1);
}

void MainWindow::on_adjustDanmakuLongestCheck_clicked()
{
    bool en = ui->adjustDanmakuLongestCheck->isChecked();
    us->setValue("danmaku/adjustDanmakuLongest", en);
    if (en)
    {
        adjustDanmakuLongest();
    }
}

void MainWindow::on_actionBuy_VIP_triggered()
{
    BuyVIPDialog* bvd = new BuyVIPDialog(rt->dataPath, ac->roomId, ac->upUid, ac->cookieUid, ac->roomTitle, ac->upName, ac->cookieUname, permissionDeadline, permissionType, this);
    connect(bvd, &BuyVIPDialog::refreshVIP, this, [=]{
        updatePermission();
    });
    bvd->show();
}

void MainWindow::on_droplight_clicked()
{
    if (rt->asFreeOnly)
    {
        showNotify("小提示", "右键可以修改文字");
        return ;
    }
    on_actionBuy_VIP_triggered();
}

void MainWindow::on_vipExtensionButton_clicked()
{
    if (rt->asFreeOnly)
    {
        QMessageBox::warning(this, "作为插件的Lite版说明", "Lite版未安装部分功能，请前往官网下载完整版");
        openLink("http://lyixi.com/?type=newsinfo&S_id=126");
        return ;
    }
    on_actionBuy_VIP_triggered();
}

void MainWindow::on_enableTrayCheck_clicked()
{
    bool en = ui->enableTrayCheck->isChecked();
    us->setValue("mainwindow/enableTray", en);
    if (en)
    {
        tray->show();
    }
    else
    {
        tray->hide();
    }
}

void MainWindow::on_toutaGiftCheck_clicked()
{
    us->setValue("danmaku/toutaGift", ui->toutaGiftCheck->isChecked());

    // 设置默认值
    if (!toutaGiftCounts.size())
    {
        QString s = "1 2 3 10 100 520 1314";
        ui->toutaGiftCountsEdit->setText(s);
        on_toutaGiftCountsEdit_textEdited(s);
    }

    if (!toutaGifts.size())
    {
        QString s = "20004 吃瓜 100\n\
                20014 比心 500\n\
                20008 冰阔落 800\n\
                30758 这个好诶 1000\n\
                30063 给大佬递茶 2000\n\
                30046 打榜 2000\n\
                30004 喵娘 5200\n\
                30064 礼花 28000\n\
                30873 花式夸夸 39000\n\
                30072 疯狂打call 52000\n\
                30087 天空之翼 100000\n\
                30924 机车娘 200000";
        us->setValue("danmaku/toutaGifts", s);
        restoreToutaGifts(s);
    }
}

void MainWindow::on_toutaGiftCountsEdit_textEdited(const QString &arg1)
{
    // 允许的数量，如：1 2 3 4 5 10 11 100 101
    us->setValue("danmaku/toutaGiftCounts", arg1);
    QStringList sl = arg1.split(" ", QString::SkipEmptyParts);
    toutaGiftCounts.clear();
    foreach (QString s, sl)
    {
        toutaGiftCounts.append(s.toInt());
    }
}

void MainWindow::on_toutaGiftListButton_clicked()
{
    // 礼物ID 礼物名字 金瓜子
    QStringList sl;
    foreach (auto gift, toutaGifts)
    {
        sl.append(QString("%1 %2 %3").arg(gift.getGiftId()).arg(gift.getGiftName()).arg(gift.getTotalCoin()));
    }

    bool ok;
    QString totalText = TextInputDialog::getText(this, "允许赠送的礼物列表", "一行一个礼物，格式：\n礼物ID 礼物名字 金瓜子数量", sl.join("\n"), &ok);
    if (!ok)
        return ;
    us->setValue("danmaku/toutaGifts", totalText);
    restoreToutaGifts(totalText);
}

void MainWindow::on_timerConnectIntervalSpin_editingFinished()
{
    us->setValue("live/timerConnectInterval", ui->timerConnectIntervalSpin->value());
    connectServerTimer->setInterval(ui->timerConnectIntervalSpin->value() * 60000);
}

void MainWindow::on_heartTimeSpin_editingFinished()
{
    us->setValue("danmaku/acquireHeartTime", ui->heartTimeSpin->value());
}

void MainWindow::on_syncShieldKeywordCheck_clicked()
{
    bool enabled = ui->syncShieldKeywordCheck->isChecked();
    us->setValue("block/syncShieldKeyword", enabled);
    if (enabled)
        pullRoomShieldKeyword();
}

void MainWindow::on_roomCoverSpacingLabel_customContextMenuRequested(const QPoint &)
{
    newFacileMenu;

    // 主播专属操作
    if (ac->cookieUid == ac->upUid)
    {
        menu->addTitle("直播设置", 0);
        menu->addAction(QIcon(":/icons/title"), "修改直播标题", [=]{
            myLiveSetTitle();
        });
        menu->addAction(QIcon(":/icons/default_cover"), "更换直播封面", [=]{
            myLiveSetCover();
        });
        menu->addAction(QIcon(":/icons/news"), "修改主播公告", [=]{
            myLiveSetNews();
        });
        menu->addAction(QIcon(":/icons/person_description"), "修改个人简介", [=]{
            myLiveSetDescription();
        });
        menu->addAction(QIcon(":/icons/tags"), "修改个人标签", [=]{
            myLiveSetTags();
        });

        menu->addTitle("快速开播", -1);
        menu->addAction(QIcon(":/icons/area"), "选择分区", [=]{
            myLiveSelectArea(false);
        });
        menu->addAction(QIcon(":/icons/video2"), "一键开播", [=]{
            if (!isLiving())
            {
                // 一键开播
                myLiveStartLive();
            }
            else
            {
                // 一键下播
                myLiveStopLive();
            }
        })->text(isLiving(), "一键下播");

        menu->addAction(QIcon(":/icons/rtmp"), "复制rtmp", [=]{
            QApplication::clipboard()->setText(myLiveRtmp);
        })->disable(myLiveRtmp.isEmpty())->lingerText("已复制");

        menu->addAction(QIcon(":/icons/token"), "复制直播码", [=]{
            QApplication::clipboard()->setText(myLiveCode);
        })->disable(myLiveCode.isEmpty())->lingerText("已复制");

        menu->split();
    }

    // 普通操作
    menu->addAction(QIcon(":/icons/save"), "保存直播间封面", [=]{
        QString oldPath = us->value("danmaku/exportPath", "").toString();
        if (!oldPath.isEmpty())
        {
            QFileInfo info(oldPath);
            if (info.isFile()) // 去掉文件名（因为可能是其他文件）
                oldPath = info.absolutePath();
        }
        QString path = QFileDialog::getSaveFileName(this, "选择保存位置", oldPath, "Images (*.jpg *.png)");
        if (path.isEmpty())
            return ;
        us->setValue("danmaku/exportPath", path);

        roomCover.save(path);
    })->disable(roomCover.isNull());

    menu->exec();
}

void MainWindow::on_upHeaderLabel_customContextMenuRequested(const QPoint &)
{
    newFacileMenu;
    menu->addAction(QIcon(":/icons/save"), "保存主播头像", [=]{
        QString oldPath = us->value("danmaku/exportPath", "").toString();
        if (!oldPath.isEmpty())
        {
            QFileInfo info(oldPath);
            if (info.isFile()) // 去掉文件名（因为可能是其他文件）
                oldPath = info.absolutePath();
        }
        QString path = QFileDialog::getSaveFileName(this, "选择保存位置", oldPath, "Images (*.jpg *.png)");
        if (path.isEmpty())
            return ;
        us->setValue("danmaku/exportPath", path);

        upFace.save(path);
    })->disable(upFace.isNull());

    menu->exec();
}

void MainWindow::on_saveEveryGiftButton_clicked()
{
    QDir dir(rt->dataPath + "gift_histories");
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void MainWindow::on_saveEveryGuardButton_clicked()
{
    QDir dir(rt->dataPath + "guard_histories");
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void MainWindow::on_saveMonthGuardButton_clicked()
{
    QDir dir(rt->dataPath + "guard_month");
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void MainWindow::on_musicConfigButton_clicked()
{
    int page = ui->musicConfigStack->currentIndex();
    if (page == 0) // 设置页面
    {
        ui->musicConfigStack->setCurrentIndex(1);
    }
    else if (page == 1) // 列表页面
    {
        ui->musicConfigStack->setCurrentIndex(0);
    }
}

void MainWindow::on_musicConfigStack_currentChanged(int arg1)
{
    us->setValue("mainwindow/musicStackIndex", arg1);
}

void MainWindow::on_addMusicToLiveButton_clicked()
{
    ui->extensionPageButton->simulateStatePress();
    ui->tabWidget->setCurrentWidget(ui->tabRemote);
    shakeWidget(ui->existExtensionsLabel);
}

void MainWindow::on_roomNameLabel_customContextMenuRequested(const QPoint &)
{
    if (ac->cookieUid != ac->upUid)
        return ;

    newFacileMenu;

    menu->addAction(QIcon(":/icons/title"), "修改直播标题", [=]{
        myLiveSetTitle();
    });

    menu->exec();
}

void MainWindow::on_upNameLabel_customContextMenuRequested(const QPoint &)
{
    newFacileMenu;

    menu->addAction(QIcon(":/icons/copy"), "复制昵称", [=]{
        QApplication::clipboard()->setText(ac->upName);
    });

    if (ac->cookieUid == ac->upUid)
    {
        menu->addAction(QIcon(":/icons/modify"), "修改昵称", [=]{

        })->disable();
    }

    menu->exec();
}

void MainWindow::on_roomAreaLabel_customContextMenuRequested(const QPoint &)
{
    if (ac->cookieUid != ac->upUid)
        return ;

    newFacileMenu;

    menu->addAction(QIcon(":/icons/area"), "修改分区", [=]{
        myLiveSelectArea(true);
    });

    menu->exec();
}

void MainWindow::on_tagsButtonGroup_customContextMenuRequested(const QPoint &)
{
    if (ac->cookieUid != ac->upUid)
        return ;

    newFacileMenu;

    menu->addAction(QIcon(":/icons/tags"), "修改个人标签", [=]{
        myLiveSetTags();
    });

    menu->exec();
}

void MainWindow::on_roomDescriptionBrowser_customContextMenuRequested(const QPoint &)
{
    if (ac->cookieUid != ac->upUid)
        return ;

    newFacileMenu;

    menu->addAction(QIcon(":/icons/person_description"), "修改个人简介", [=]{
        myLiveSetDescription();
    });

    menu->exec();
}

void MainWindow::on_upLevelLabel_customContextMenuRequested(const QPoint &)
{
    if (ac->cookieUid != ac->upUid)
        return ;

    newFacileMenu;

    menu->addAction(QIcon(":/icons/person_description"), "修改个人签名", [=]{

    })->disable();

    menu->exec();
}

void MainWindow::on_refreshExtensionListButton_clicked()
{
    loadWebExtensionList();
    qInfo() << "网页扩展数量：" << ui->extensionListWidget->count();
}

void MainWindow::on_droplight_customContextMenuRequested(const QPoint &)
{
    newFacileMenu;

    menu->addAction(QIcon(":/icons/heart2"), "自定义文字", [=]{
        bool ok;
        QString s = QInputDialog::getText(this, "自定义文字", "高度自定义显示文字", QLineEdit::Normal, permissionText, &ok);
        if (!ok)
            return ;
        us->setValue("mainwindow/permissionText", permissionText = s);
        ui->droplight->setText(permissionText);
        ui->droplight->adjustMinimumSize();
    })->disable(!hasPermission() && !rt->asPlugin);

    menu->exec();
}

void MainWindow::on_autoUpdateCheck_clicked()
{
    us->setValue("runtime/autoUpdate", ui->autoUpdateCheck->isChecked());
}

void MainWindow::on_showChangelogCheck_clicked()
{
    us->setValue("runtime/showChangelog", ui->showChangelogCheck->isChecked());
}

void MainWindow::on_updateBetaCheck_clicked()
{
    us->setValue("runtime/updateBeta", ui->updateBetaCheck->isChecked());
}

void MainWindow::on_dontSpeakOnPlayingSongCheck_clicked()
{
    us->setValue("danmaku/dontSpeakOnPlayingSong", ui->dontSpeakOnPlayingSongCheck->isChecked());
}

void MainWindow::on_shieldKeywordListButton_clicked()
{
    QDesktopServices::openUrl(QUrl(SERVER_DOMAIN + "/web/keyword/list"));
}

void MainWindow::on_saveEveryGuardButton_customContextMenuRequested(const QPoint &pos)
{
    newFacileMenu;

    menu->addAction("导出每月表格", [=]{
        QString oldPath = us->value("danmaku/exportPath", "").toString();
        QString path = QFileDialog::getSaveFileName(this, "选择导出位置", oldPath, "Tables (*.csv)");
        if (path.isEmpty())
            return ;
        us->setValue("danmaku/exportPath", path);

        exportAllGuardsByMonth(path);
    });

    menu->exec();
}

/**
 * 导出每个月所有舰长的表格
 * 横向：月份
 * 纵向：UID 昵称 备注
 */
void MainWindow::exportAllGuardsByMonth(QString exportPath)
{
    QString readPath = rt->dataPath + "guard_histories/" + ac->roomId + ".csv";
    if (!isFileExist(readPath))
    {
        showError("不存在舰长记录", readPath);
        return ;
    }

    /// 购买舰长的记录表
    struct GuardTrade
    {
        QString date;
        QString nickname;
        QString giftName;
        int count;
        qint64 uid;
        QString remark;
    };

    /// 读取表格数据
    // 默认表格格式：日期 时间 昵称 礼物 数量 累计 UID 备注
    QString content = readTextFileAutoCodec(readPath);
    QStringList lines = content.split("\n", QString::SkipEmptyParts);
    QList<GuardTrade> trades;
    for (int i = 1; i < lines.size(); ++i)
    {
        QStringList cells = lines.at(i).split(",");
        if (cells.size() < 7)
        {
            qWarning() << "不符合的格式：" << lines.at(i);
            continue;
        }
        GuardTrade trade{cells[0], cells[2], cells[3], cells[4].toInt(), cells[6].toLongLong(), cells.size() >= 8 ? cells[7] : ""};
        trades.append(trade);
    }

    // 获取所有月份
    QList<QString> months; // 月份表，格式：2021-1,2021-2,2021-3...
    QString prevMonth = "";
    for (GuardTrade& trade: trades)
    {
        QString month = QDate::fromString(trade.date, "yyyy-MM-dd").toString("yyyy.MM");
        trade.date = month;
        if (prevMonth == month)
            continue;
        months.append(month);
        prevMonth = month;
    }

    // 获取所有用户
    QList<qint64> users;
    QList<QString> nicknames;
    for (const GuardTrade& trade: trades)
    {
        if (!users.contains(trade.uid))
        {
            users.append(trade.uid);
            nicknames.append(trade.nickname); // 只用第一次上船时的昵称
        }
    }

    /// 映射向量
    int guardCount[users.size()][months.size()][3];
    memset(guardCount, 0, sizeof (int) * users.size() * months.size() * 3);

    const int ZD = 0; // 总督索引
    const int TD = 1; // 提督
    const int JZ = 2; // 舰长

    for (const GuardTrade& trade: trades)
    {
        int userIndex = users.indexOf(trade.uid);
        int monthIndex = months.indexOf(trade.date);
        Q_ASSERT(userIndex > -1);
        Q_ASSERT(monthIndex > -1);

        if (trade.giftName == "总督")
            guardCount[userIndex][monthIndex][ZD] += trade.count;
        else if (trade.giftName == "提督")
            guardCount[userIndex][monthIndex][TD] += trade.count;
        else if (trade.giftName == "舰长")
            guardCount[userIndex][monthIndex][JZ] += trade.count;
        else
            qWarning() << "未知的礼物：" << trade.giftName;
    }

    /// 导出表格
    // 标题
    QString fullText = "UID,昵称";
    for (int i = 0; i < months.size(); i++)
        fullText += "," + months.at(i);

    // 内容每一行
    for (int i = 0; i < users.size(); i++)
    {
        // 添加用户ID和昵称列
        fullText += "\n" + snum(users.at(i)) + "," + nicknames.at(i);
        // 添加每次上船的标记
        for (int j = 0; j < months.size(); j++)
        {
            QStringList cellList;
            if (guardCount[i][j][ZD] != 0)
                cellList.append("总督" + (guardCount[i][j][ZD] > 1 ? snum(guardCount[i][j][ZD]) : ""));
            if (guardCount[i][j][TD] != 0)
                cellList.append("提督" + (guardCount[i][j][TD] > 1 ? snum(guardCount[i][j][TD]) : ""));
            if (guardCount[i][j][JZ] != 0)
                cellList.append("舰长" + (guardCount[i][j][JZ] > 1 ? snum(guardCount[i][j][JZ]) : ""));
            fullText += "," + cellList.join(" / ");
        }
    }

    writeTextFile(exportPath, fullText, recordFileCodec);
}


void MainWindow::on_MSAreaCodeEdit_editingFinished()
{
    us->setValue("mstts/areaCode", ui->MSAreaCodeEdit->text());
    if (msTTS)
    {
        msTTS->setAreaCode(ui->MSAreaCodeEdit->text());
    }
}

void MainWindow::on_MSSubscriptionKeyEdit_editingFinished()
{
    us->setValue("mstts/subscriptionKey", ui->MSSubscriptionKeyEdit->text());
    if (msTTS)
    {
        msTTS->setSubscriptionKey(ui->MSSubscriptionKeyEdit->text());
    }
}

void MainWindow::on_MS_TTS__SSML_Btn_clicked()
{
    bool ok;
    QString fmt = TextInputDialog::getText(this, "微软语音SSML格式", "使用 %text% 替换要朗读的文字。<a href='https://docs.microsoft.com/zh-cn/azure/cognitive-services/speech-service/speech-synthesis-markup?tabs=csharp#create-an-ssml-document'>SSML文档</a>", msTTSFormat, &ok);
    if (!ok)
        return ;
    if (fmt.trimmed().isEmpty())
        fmt = DEFAULT_MS_TTS_SSML_FORMAT;
    us->setValue("mstts/format", msTTSFormat = fmt);
}

void MainWindow::on_receivePrivateMsgCheck_clicked()
{
    us->setValue("privateMsg/enabled", ui->receivePrivateMsgCheck->isChecked());
}

void MainWindow::on_receivePrivateMsgCheck_stateChanged(int arg1)
{
    if (arg1)
    {
        if (!privateMsgTimer)
        {
            // 初始化时钟
            privateMsgTimer = new QTimer(this);
            privateMsgTimer->setInterval(5000);
            privateMsgTimer->setSingleShot(false);
            connect(privateMsgTimer, &QTimer::timeout, this, [=]{
                refreshPrivateMsg();
            });
        }

        privateMsgTimer->start();
    }
    else
    {
        if (privateMsgTimer)
            privateMsgTimer->stop();
    }
}

void MainWindow::on_processUnreadMsgCheck_clicked()
{
    us->setValue("privateMsg/processUnread", ui->processUnreadMsgCheck->isChecked());
}

void MainWindow::on_TXSecretIdEdit_editingFinished()
{
    QString text = ui->TXSecretIdEdit->text();
    us->setValue("tx_nlp/secretId", text);
    TxNlp::instance()->setSecretId(text);
}

void MainWindow::on_TXSecretKeyEdit_editingFinished()
{
    QString text = ui->TXSecretKeyEdit->text();
    us->setValue("tx_nlp/secretKey", text);
    TxNlp::instance()->setSecretKey(text);
}

void MainWindow::on_saveDanmakuToFileButton_clicked()
{
    QDir dir(rt->dataPath + "danmaku_histories");
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void MainWindow::on_calculateDailyDataButton_clicked()
{
    QDir dir(rt->dataPath + "live_daily");
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void MainWindow::on_syntacticSugarCheck_clicked()
{
    us->setValue("programming/syntacticSugar", ui->syntacticSugarCheck->isChecked());
}

void MainWindow::on_forumButton_clicked()
{
    QDesktopServices::openUrl(QUrl("http://live.lyixi.com"));
}

void MainWindow::on_complexCalcCheck_clicked()
{
    us->setValue("programming/compexCalc", ui->complexCalcCheck->isChecked());
}

/**
 * 饭贩好评
 * 需要 Cookie 带有 PEA_AU 字段
 */
void MainWindow::on_positiveVoteCheck_clicked()
{
    if (ac->browserData.isEmpty()) // 未登录
    {
        QDesktopServices::openUrl(QUrl("https://play-live.bilibili.com/details/1653383145397"));
        return ;
    }

    us->setValue("ask/positiveVote", true);
    if (_fanfanLike > 0) // 取消好评
    {
        if (QMessageBox::question(this, "取消好评", "您已为神奇弹幕点赞，要残忍地取消吗？", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        {
            QTimer::singleShot(200, [=]{
                ui->positiveVoteCheck->setChecked(true);
            });
            return ;
        }
    }

    if (ac->browserCookie.contains("PEA_AU=")) // 浏览器复制的cookie，一键好评
    {
        positiveVote();
    }
    else // 先获取登录信息，再进行好评
    {
        fanfanLogin();
    }
}

void MainWindow::on_saveLogCheck_clicked()
{
    us->setValue("debug/logFile", ui->saveLogCheck->isChecked());
    if (ui->saveLogCheck->isChecked())
    {
        showNotify("打印日志", "重启生效：安装路径/debug.log");
    }
}

void MainWindow::on_stringSimilarCheck_clicked()
{
    us->setValue("programming/stringSimilar", us->useStringSimilar = ui->stringSimilarCheck->isChecked());
}

void MainWindow::on_onlineRankListWidget_itemDoubleClicked(QListWidgetItem *item)
{
    openLink("https://space.bilibili.com/" + snum(item->data(Qt::UserRole).toLongLong()));
}

void MainWindow::on_actionShow_Gift_List_triggered()
{
    if (pl->allGiftMap.empty())
    {
        getGiftList();
        QTimer::singleShot(2000, [=]{
            if (pl->allGiftMap.size())
                on_actionShow_Gift_List_triggered();
        });
        return ;
    }

    // 创建MV
    QTableView* view = new QTableView(nullptr);
    QStandardItemModel* model = new QStandardItemModel(view);
    view->setModel(model);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // view->setAttribute(Qt::WA_ShowModal, true);
    view->setAttribute(Qt::WA_DeleteOnClose, true);
    view->setWindowFlags(Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint | Qt::Dialog);
    view->setWordWrap(true);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setWindowTitle("当前直播间的礼物");

    // 读取数据
    model->setColumnCount(4);
    model->setRowCount(pl->allGiftMap.size());
    model->setHorizontalHeaderLabels({"ID", "名称", "价格", "描述"});
    QColor goldColor("#CD7F32");
    QColor silverColor("#C0C0C0");
    int row = 0;
    for (auto it = pl->allGiftMap.begin(); it != pl->allGiftMap.end(); it++)
    {
        const LiveDanmaku& info = it.value();
        model->setItem(row, 0, new QStandardItem(snum(info.getGiftId())));
        model->setItem(row, 1, new QStandardItem(info.getGiftName()));
        if (info.getTotalCoin() > 0)
        {
            auto item = new QStandardItem(snum(info.getTotalCoin()));
            if (info.isGoldCoin())
                item->setForeground(goldColor);
            else if (info.isSilverCoin())
                item->setForeground(silverColor);
            model->setItem(row, 2, item);
        }
        model->setItem(row, 3, new QStandardItem(info.extraJson.value("desc").toString()));
        row++;
    }

    // 显示控件
    view->setGeometry(this->geometry());
    view->show();
}

void MainWindow::on_liveOpenCheck_clicked()
{
    us->set("live-open/enabled", ui->liveOpenCheck->isChecked());
    if (ui->liveOpenCheck->isChecked())
    {
        if (isLiving())
            liveOpenService->start();
    }
    else
    {
        if (liveOpenService)
            liveOpenService->endIfStarted();
    }
}

void MainWindow::on_identityCodeEdit_editingFinished()
{
    ac->identityCode = ui->identityCodeEdit->text();
    us->set("live-open/identityCode", ac->identityCode);
}

void MainWindow::on_recordDataButton_clicked()
{
    QDir dir(rt->dataPath + "record");
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void MainWindow::on_actionLogout_triggered()
{
    if (QMessageBox::question(this, "退出登录", "是否退出登录？\n这将清除该账号cookie，需要重新登录方可使用",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return ;

    us->setValue("danmaku/browserCookie", ac->browserCookie = "");
    us->setValue("danmaku/browserData", ac->browserData = "");
    ac->userCookies = QVariant();
    ac->csrf_token = "";
    ac->cookieUid = "";
    ac->cookieUname = "";
    ac->cookieToken = "";
    ac->cookieULevel = 0;
    ac->cookieGuardLevel = 0;

    ui->robotNameButton->setText("点此重新登录");
    ui->robotNameButton->adjustMinimumSize();
    ui->robotInfoWidget->setMinimumWidth(ui->robotNameButton->width());
}

void MainWindow::on_saveToSqliteCheck_clicked()
{
    if (ui->saveToSqliteCheck->isChecked() && !hasPermission())
    {
        on_actionBuy_VIP_triggered();
        ui->saveToSqliteCheck->setChecked(false);
        return ;
    }

    us->set("db/sqlite", saveToSqlite = ui->saveToSqliteCheck->isChecked());

    ui->saveCmdToSqliteCheck->setEnabled(saveToSqlite);
    if (saveToSqlite)
    {
        sqlService.open();
    }
    else
    {
        sqlService.close();
    }
}

void MainWindow::on_databaseQueryButton_clicked()
{
    on_actionQueryDatabase_triggered();
}

void MainWindow::on_actionQueryDatabase_triggered()
{
    showSqlQueryResult("");
}

void MainWindow::on_saveCmdToSqliteCheck_clicked()
{
    us->set("db/cmd", saveCmdToSqlite = ui->saveCmdToSqliteCheck->isChecked());
}

void MainWindow::on_recordFormatCheck_clicked()
{
    us->set("record/format", ui->recordFormatCheck->isChecked());
    if (ui->recordFormatCheck->isChecked() && rt->ffmpegPath.isEmpty())
    {
        // 尝试自动获取 ffmpeg.exe 位置
        QStringList environmentList = QProcess::systemEnvironment();
#ifdef Q_OS_WIN
        const QString app = "ffmpeg.exe";
#else
        const QString app = "ffmpeg";
#endif
        rt->ffmpegPath = "";
        foreach (QString environment, environmentList)
        {
            if (environment.startsWith("Path="))
            {
                QStringList sl = environment.right(environment.length() - 5).split(";", QString::SkipEmptyParts);
                foreach (QString d, sl)
                {
                    QString path = QDir(d).absoluteFilePath(app);
                    if (QFileInfo(path).exists())
                    {
                        qInfo() << "自动设置" << app << "位置：" << path;
                        rt->ffmpegPath = path;
                        us->setValue("record/ffmpegPath", path);
                        break;
                    }
                }
                break;
            }
        }

        // 手动获取
        if (rt->ffmpegPath.isEmpty())
        {
            on_ffmpegButton_clicked();
        }

        // 取消勾选
        if (rt->ffmpegPath.isEmpty())
        {
            ui->recordFormatCheck->setChecked(false);
            us->set("record/format", false);
        }
    }
}

void MainWindow::on_ffmpegButton_clicked()
{
    QString oldPath = us->value("recent/ffmpegPath", "").toString();
    QString path = QFileDialog::getOpenFileName(this, "选择 ffmpeg.exe 位置", oldPath, "应用程序 (*.exe)");
    if (path.isEmpty())
        return ;
    us->setValue("recent/ffmpegPath", path);
    us->setValue("record/ffmpegPath", path);
    rt->ffmpegPath = path;
}

void MainWindow::on_closeGuiCheck_clicked()
{
    us->closeGui = ui->closeGuiCheck->isChecked();
    us->setValue("mainwindow/closeGui", us->closeGui = ui->closeGuiCheck->isChecked());

    if (us->closeGui)
    {
        // 关闭已有的GUI效果
        auto lw = ui->onlineRankListWidget;
        while (lw->count())
        {
            auto item = lw->item(0);
            auto widget = lw->itemWidget(item);
            lw->takeItem(0);
            delete widget;
        }
        ui->onlineRankDescLabel->setPixmap(QPixmap());

        lw = ui->giftListWidget;
        while (lw->count())
        {
            auto item = lw->item(0);
            auto widget = lw->itemWidget(item);
            lw->takeItem(0);
            delete widget;
        }
    }
}
