#include "mainwindow.h"

#include <map>
#include <mutex>

#include <QWebEnginePage>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonObject>

#include <QTextDocument>
#include <QLineEdit>
#include <QWebEngineProfile>
#include <QKeyEvent>
#include <QMenu>
#include <QStandardItemModel>
#include <QFontDatabase>
#include <QDesktopServices>
#include <QSettings>

#include "WebSocketClient.h"
#include "JavascriptWrapper.h"

#include "uploader.h"
#include "check.h"
#include "StopApplication.h"
#include "duration.h"
#include "Log.h"
#include "utils.h"
#include "SlotWrapper.h"
#include "Paths.h"

#include "mhurlschemehandler.h"

#include "auth/AuthJavascript.h"
#include "auth/Auth.h"
#include "Messenger/MessengerJavascript.h"
#include "transactions/TransactionsJavascript.h"
#include "Initializer/InitializerJavascript.h"

#include "machine_uid.h"

const static QString DEFAULT_USERNAME = "_unregistered";

bool EvFilter::eventFilter(QObject * watched, QEvent * event) {
    QToolButton * button = qobject_cast<QToolButton*>(watched);
    if (!button) {
        return false;
    }

    if (event->type() == QEvent::Enter) {
        // The push button is hovered by mouse
        button->setIcon(icoHover);
        return true;
    } else if (event->type() == QEvent::Leave){
        // The push button is not hovered by mouse
        button->setIcon(icoActive);
        return true;
    }

    return false;
}

MainWindow::MainWindow(initializer::InitializerJavascript &initializerJs, QWidget *parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
    , last_htmls(Uploader::getLastHtmlVersion())
    , currentUserName(DEFAULT_USERNAME)
{
    ui->setupUi(this);

    CHECK(connect(this, &MainWindow::setJavascriptWrapper, this, &MainWindow::onSetJavascriptWrapper), "not connect onSetJavascriptWrapper");
    CHECK(connect(this, &MainWindow::setAuth, this, &MainWindow::onSetAuth), "not connect onSetAuth");
    CHECK(connect(this, &MainWindow::setMessengerJavascript, this, &MainWindow::onSetMessengerJavascript), "not connect onSetMessengerJavascript");
    CHECK(connect(this, &MainWindow::setTransactionsJavascript, this, &MainWindow::onSetTransactionsJavascript), "not connect onSetTransactionsJavascript");
    CHECK(connect(this, &MainWindow::initFinished, this, &MainWindow::onInitFinished), "not connect onInitFinished");

    qRegisterMetaType<SignalFunc>("SignalFunc");
    qRegisterMetaType<SetJavascriptWrapperCallback>("SetJavascriptWrapperCallback");
    qRegisterMetaType<SetAuthCallback>("SetAuthCallback");
    qRegisterMetaType<SetMessengerJavascriptCallback>("SetMessengerJavascriptCallback");
    qRegisterMetaType<SetTransactionsJavascriptCallback>("SetTransactionsJavascriptCallback");

    shemeHandler = new MHUrlSchemeHandler(this);
    QWebEngineProfile::defaultProfile()->installUrlSchemeHandler(QByteArray("mh"), shemeHandler);

    hardwareId = QString::fromStdString(::getMachineUid());

    configureMenu();

    pagesMappings.setFullPagesPath(last_htmls.fullPath);
    const std::string contentMappings = readFile(makePath(last_htmls.fullPath, "core/routes.json"));
    LOG << "Set mappings2 " << QString::fromStdString(contentMappings).simplified();
    pagesMappings.setMappings(QString::fromStdString(contentMappings));

    loadFile("core/loader/index.html");

    client.setParent(this);
    CHECK(connect(&client, &SimpleClient::callbackCall, this, &MainWindow::onCallbackCall), "not connect callbackCall");

    qRegisterMetaType<WindowEvent>("WindowEvent");

    channel = std::make_unique<QWebChannel>(ui->webView->page());
    ui->webView->page()->setWebChannel(channel.get());
    channel->registerObject(QString("initializer"), &initializerJs);

    ui->webView->setContextMenuPolicy(Qt::CustomContextMenu);
    CHECK(connect(ui->webView, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onShowContextMenu(const QPoint &))), "not connect customContextMenuRequested");

    //CHECK(connect(ui->webView->page(), &QWebEnginePage::loadFinished, this, &MainWindow::onBrowserLoadFinished), "not connect loadFinished");

    CHECK(connect(ui->webView->page(), &QWebEnginePage::urlChanged, this, &MainWindow::onBrowserLoadFinished), "not connect loadFinished");

    qtimer.setInterval(milliseconds(hours(1)).count());
    qtimer.setSingleShot(false);
    CHECK(connect(&qtimer, SIGNAL(timeout()), this, SLOT(onUpdateMhsReferences())), "not connect timeout");
    qtimer.start();

    emit onUpdateMhsReferences();
}

void MainWindow::onSetJavascriptWrapper(JavascriptWrapper *jsWrapper1, const SignalFunc &signal, const SetJavascriptWrapperCallback &callback) {
BEGIN_SLOT_WRAPPER
    const TypedException exception = apiVrapper2([&, this] {
        CHECK(jsWrapper1 != nullptr, "Incorrect jsWrapper1");
        jsWrapper = jsWrapper1;
        jsWrapper->setWidget(this);
        CHECK(connect(jsWrapper, SIGNAL(jsRunSig(QString)), this, SLOT(onJsRun(QString))), "not connect jsRunSig");
        channel->registerObject(QString("mainWindow"), jsWrapper);
        CHECK(connect(jsWrapper, SIGNAL(setHasNativeToolbarVariableSig()), this, SLOT(onSetHasNativeToolbarVariable())), "not connect setHasNativeToolbarVariableSig");
        CHECK(connect(jsWrapper, SIGNAL(setCommandLineTextSig(QString)), this, SLOT(onSetCommandLineText(QString))), "not connect setCommandLineTextSig");
        CHECK(connect(jsWrapper, SIGNAL(setMappingsSig(QString)), this, SLOT(onSetMappings(QString))), "not connect setMappingsSig");
        CHECK(connect(jsWrapper, SIGNAL(lineEditReturnPressedSig(QString)), this, SLOT(onEnterCommandAndAddToHistory(QString))), "not connect lineEditReturnPressedSig");
        doConfigureMenu();
    });
    emit signal(std::bind(callback, exception));
END_SLOT_WRAPPER
}

void MainWindow::onSetAuth(auth::AuthJavascript *authJavascript, auth::Auth *authManager, const SignalFunc &signal, const SetAuthCallback &callback) {
BEGIN_SLOT_WRAPPER
    const TypedException exception = apiVrapper2([&, this] {
        CHECK(authJavascript != nullptr, "Incorrect authJavascript");
        CHECK(connect(authJavascript, SIGNAL(jsRunSig(QString)), this, SLOT(onJsRun(QString))), "not connect jsRunSig");
        channel->registerObject(QString("auth"), authJavascript);

        CHECK(authManager != nullptr, "Incorrect authManager");
        CHECK(connect(authManager, &auth::Auth::logined, this, &MainWindow::onLogined), "not connect onLogined");
        emit authManager->reEmit();
    });
    emit signal(std::bind(callback, exception));
END_SLOT_WRAPPER
}

void MainWindow::onSetMessengerJavascript(messenger::MessengerJavascript *messengerJavascript, const SignalFunc &signal, const SetMessengerJavascriptCallback &callback) {
BEGIN_SLOT_WRAPPER
    const TypedException exception = apiVrapper2([&, this] {
        CHECK(messengerJavascript != nullptr, "Incorrect messengerJavascript");
        CHECK(connect(messengerJavascript, SIGNAL(jsRunSig(QString)), this, SLOT(onJsRun(QString))), "not connect jsRunSig");
        channel->registerObject(QString("messenger"), messengerJavascript);
    });
    emit signal(std::bind(callback, exception));
END_SLOT_WRAPPER
}

void MainWindow::onSetTransactionsJavascript(transactions::TransactionsJavascript *transactionsJavascript, const SignalFunc &signal, const SetTransactionsJavascriptCallback &callback) {
BEGIN_SLOT_WRAPPER
    const TypedException exception = apiVrapper2([&, this] {
        CHECK(transactionsJavascript != nullptr, "Incorrect transactionsJavascript");
        CHECK(connect(transactionsJavascript, SIGNAL(jsRunSig(QString)), this, SLOT(onJsRun(QString))), "not connect jsRunSig");
        channel->registerObject(QString("transactions"), transactionsJavascript);
    });
    emit signal(std::bind(callback, exception));
END_SLOT_WRAPPER
}

void MainWindow::onInitFinished() {
BEGIN_SLOT_WRAPPER
    isInitFinished = true;
    if (currentUserName == DEFAULT_USERNAME) {
        enterCommandAndAddToHistory("app://Login", true, true);
    } else {
        enterCommandAndAddToHistory("app://MetaApps", true, true);
    }
    ui->grid_layout->show();
END_SLOT_WRAPPER
}

void MainWindow::onUpdateMhsReferences() {
BEGIN_SLOT_WRAPPER
    QSettings settings(getSettingsPath(), QSettings::IniFormat);
    CHECK(settings.contains("dns/metahash"), "dns/metahash setting not found");
    CHECK(settings.contains("timeouts_sec/dns_metahash"), "timeouts_sec setting not found");
    const seconds timeout(settings.value("timeouts_sec/dns_metahash").toInt());

    client.sendMessageGet(QUrl(settings.value("dns/metahash").toString()), [this](const std::string &response, const SimpleClient::ServerException &exception) {
        LOG << "Set mappings mh " << QString::fromStdString(response).simplified();
        CHECK(!exception.isSet(), "Server error: " + exception.description);
        pagesMappings.setMappingsMh(QString::fromStdString(response));
    }, timeout);

END_SLOT_WRAPPER
}

void MainWindow::onCallbackCall(SimpleClient::ReturnCallback callback) {
BEGIN_SLOT_WRAPPER
    callback();
END_SLOT_WRAPPER
}

void MainWindow::doConfigureMenu() {
    CHECK(jsWrapper != nullptr, "jsWrapper not set");
    CHECK(connect(ui->commandLine->lineEdit(), &QLineEdit::editingFinished, [this]{
        countFocusLineEditChanged++;
    }), "Not connect editingFinished");
    CHECK(connect(ui->commandLine->lineEdit(), &QLineEdit::textEdited, [this](const QString &text){
        lineEditUserChanged = true;
        emit jsWrapper->sendCommandLineMessageToWssSig(hardwareId, ui->userButton->text(), countFocusLineEditChanged, text, false, true);
    }), "Not connect textChanged");
    CHECK(connect(ui->commandLine->lineEdit(), &QLineEdit::textChanged, [this](const QString &text){
        if (!lineEditUserChanged) {
            emit jsWrapper->sendCommandLineMessageToWssSig(hardwareId, ui->userButton->text(), countFocusLineEditChanged, text, false, false);
        }
    }), "Not connect textChanged");
    CHECK(connect(ui->commandLine->lineEdit(), &QLineEdit::returnPressed, [this]{
        emit jsWrapper->sendCommandLineMessageToWssSig(hardwareId, ui->userButton->text(), countFocusLineEditChanged, ui->commandLine->lineEdit()->text(), true, true);
        ui->commandLine->lineEdit()->setText(currentTextCommandLine);
    }), "Not connect returnPressed");
    CHECK(connect(ui->commandLine->lineEdit(), &QLineEdit::editingFinished, [this](){
        lineEditUserChanged = false;
    }), "Not connect textChanged");
}

void MainWindow::configureMenu() {
    ui->grid_layout->hide();

    this->setStyleSheet("QMainWindow {background: rgb(242,242,242);}");

    QFontDatabase::addApplicationFont(":/resources/Roboto-Regular.ttf");
#ifdef TARGET_OS_MAC
    const int fontSize = 12;
#else
    const int fontSize = 10;
#endif
    QFont font("Roboto", fontSize);
    font.setBold(false);
    font.setItalic(false);
    font.setKerning(false);

    auto configureBrowserButton = [](QAbstractButton *button) {
        button->setStyleSheet(
            "QAbstractButton {background-color: transparent; border-radius: 5px;} "
            "QAbstractButton:hover { background-color: #e6e6e6; border-radius: 5px;}"
        );
    };

    auto configureMenuButton = [this, &font](QAbstractButton *button, const QString &icoActive, const QString icoHover) {
        button->setFont(font);
        button->setStyleSheet(
            "QAbstractButton {color: rgb(99, 99, 99); background-color: transparent; border-radius: 5px;} "
            "QAbstractButton:hover { background-color: #4F4F4F; color: white; border-radius: 5px;} "
            "QToolButton::menu-indicator { image: none; }"
        );

        button->installEventFilter(new EvFilter(this, icoActive, icoHover));
    };

    //configureMenuButton(ui->buyButton, ":/resources/svg/Buy_MHC.svg", ":/resources/svg/Buy_MHC_white.svg");
    configureMenuButton(ui->metaAppsButton, ":/resources/svg/MetaApps.svg", ":/resources/svg/MetaApps_white.svg");
    configureMenuButton(ui->metaWalletButton, ":/resources/svg/MetaWallet.svg", ":/resources/svg/MetaWallet_white.svg");
    configureMenuButton(ui->userButton, ":/resources/svg/user.svg", ":/resources/svg/user_white.svg");

    configureBrowserButton(ui->backButton);
    configureBrowserButton(ui->forwardButton);
    configureBrowserButton(ui->refreshButton);

    QFont fontCommandLine("Roboto", 12);
    fontCommandLine.setBold(false);
    fontCommandLine.setItalic(false);
    fontCommandLine.setKerning(false);

    ui->commandLine->setFont(fontCommandLine);
    ui->commandLine->setStyleSheet(
        "QComboBox {color: rgb(99, 99, 99); border-radius: 14px; padding-left: 14px; padding-right: 14px; } "
        "QComboBox::drop-down {padding-top: 10px; padding-right: 10px; width: 10px; height: 10px; image: url(:/resources/svg/arrow.svg);}"
    );
    ui->commandLine->setAttribute(Qt::WA_MacShowFocusRect, 0);

    registerCommandLine();

    CHECK(connect(ui->backButton, &QToolButton::pressed, [this] {
        historyPos--;
        enterCommandAndAddToHistory(history.at(historyPos - 1), false, false);
        ui->backButton->setEnabled(historyPos > 1);
        ui->forwardButton->setEnabled(historyPos < history.size());
    }), "not connect backButton::pressed");
    ui->backButton->setEnabled(false);

    CHECK(connect(ui->forwardButton, &QToolButton::pressed, [this]{
        historyPos++;
        enterCommandAndAddToHistory(history.at(historyPos - 1), false, false);
        ui->backButton->setEnabled(historyPos > 1);
        ui->forwardButton->setEnabled(historyPos < history.size());
    }), "not connect forwardButton::pressed");
    ui->forwardButton->setEnabled(false);

    CHECK(connect(ui->refreshButton, SIGNAL(pressed()), ui->webView, SLOT(reload())), "not connect refreshButton::pressed");

    CHECK(connect(ui->userButton, &QAbstractButton::pressed, [this]{
        onEnterCommandAndAddToHistory("Settings");
    }), "not connect userButton::pressed");

    /*CHECK(connect(ui->buyButton, &QAbstractButton::pressed, [this]{
        onEnterCommandAndAddToHistory("BuyMHC");
    }), "not connect buyButton::pressed");*/

    CHECK(connect(ui->metaWalletButton, &QAbstractButton::pressed, [this]{
        onEnterCommandAndAddToHistory("Wallet");
    }), "not connect metaWalletButton::pressed");

    CHECK(connect(ui->metaAppsButton, &QAbstractButton::pressed, [this]{
        onEnterCommandAndAddToHistory("MetaApps");
    }), "not connect metaAppsButton::pressed");
}

void MainWindow::registerCommandLine() {
    CHECK(connect(ui->commandLine, SIGNAL(currentIndexChanged(const QString&)), this, SLOT(onEnterCommandAndAddToHistoryNoDuplicate(const QString&))), "not connect currentIndexChanged");
}

void MainWindow::unregisterCommandLine() {
    CHECK(disconnect(ui->commandLine, SIGNAL(currentIndexChanged(const QString&)), this, SLOT(onEnterCommandAndAddToHistoryNoDuplicate(const QString&))), "not disconnect currentIndexChanged");
}

void MainWindow::onEnterCommandAndAddToHistory(const QString &text) {
BEGIN_SLOT_WRAPPER
    enterCommandAndAddToHistory(text, true, false);
END_SLOT_WRAPPER
}

void MainWindow::onEnterCommandAndAddToHistoryNoDuplicate(const QString &text) {
BEGIN_SLOT_WRAPPER
    enterCommandAndAddToHistory(text, true, true);
END_SLOT_WRAPPER
}

void MainWindow::enterCommandAndAddToHistory(const QString &text1, bool isAddToHistory, bool isNoEnterDuplicate) {
    LOG << "command line " << text1;

    const QString HTTP_1_PREFIX = "http://";
    const QString HTTP_2_PREFIX = "https://";

    QString text = text1;
    if (text.endsWith('/')) {
        text = text.left(text.size() - 1);
    }

    if (isNoEnterDuplicate && text == currentTextCommandLine) {
        return;
    }

    const PageInfo pageInfo = pagesMappings.find(text);
    const QString &reference = pageInfo.page;

    if (reference.isNull() || reference.isEmpty()) {
        if (text.startsWith(APP_URL)) {
            text = text.mid(APP_URL.size());
        }
        QTextDocument td;
        td.setHtml(text);
        const QString plained = td.toPlainText();
        const PageInfo &searchPage = pagesMappings.getSearchPage();
        QString link = searchPage.page;
        link += plained;
        LOG << "Search page " << link;
        addElementToHistoryAndCommandLine(searchPage.printedName + ":" + text, isAddToHistory, true);
        loadFile(link);
    } else if (reference.startsWith(METAHASH_URL)) {
        QString uri = reference.mid(METAHASH_URL.size());
        const int pos = uri.indexOf('/');
        QString other;
        if (pos != -1) {
            other = uri.mid(pos);
            uri = uri.left(pos);
        }

        QString clText;
        if (pageInfo.printedName.isNull() || pageInfo.printedName.isEmpty()) {
            clText = reference;
        } else {
            clText = pageInfo.printedName + other;
        }
        addElementToHistoryAndCommandLine(clText, isAddToHistory, true);
        loadUrl(reference);
    } else {
        QString clText;
        if (pageInfo.printedName.isNull() || pageInfo.printedName.isEmpty()) {
            clText = text;
        } else {
            clText = pageInfo.printedName;
        }

        if (pageInfo.isExternal) {
            qtOpenInBrowser(reference);
        } else if (reference.startsWith(HTTP_1_PREFIX) || reference.startsWith(HTTP_2_PREFIX) || !pageInfo.isLocalFile) {
            addElementToHistoryAndCommandLine(clText, isAddToHistory, true);
            loadUrl(reference);
        } else {
            addElementToHistoryAndCommandLine(clText, isAddToHistory, true);
            loadFile(reference);
        }
    }
}

void MainWindow::qtOpenInBrowser(QString url) {
    LOG << "Open url in default browser " << url;
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::onShowContextMenu(const QPoint &point) {
BEGIN_SLOT_WRAPPER
    QMenu contextMenu(tr("Context menu"), this);

    contextMenu.addAction("cut", []{
        QWidget* focused = QApplication::focusWidget();
        if(focused != 0) {
            QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, Qt::Key_X, Qt::ControlModifier));
            QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, Qt::Key_X, Qt::ControlModifier));
        }
    });

    contextMenu.addAction("copy", []{
        QWidget* focused = QApplication::focusWidget();
        if(focused != 0) {
            QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier));
            QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier));
        }
    });

    contextMenu.addAction("paste", []{
        QWidget* focused = QApplication::focusWidget();
        if(focused != 0) {
            QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier));
            QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, Qt::Key_V, Qt::ControlModifier));
        }
    });

    contextMenu.exec(mapToGlobal(point));
END_SLOT_WRAPPER
}

void MainWindow::processEvent(WindowEvent event) {
BEGIN_SLOT_WRAPPER
    if (event == WindowEvent::RELOAD_PAGE) {
        softReloadPage();
    }
END_SLOT_WRAPPER
}

void MainWindow::updateAppEvent(const QString appVersion, const QString reference, const QString message) {
BEGIN_SLOT_WRAPPER
    const QString currentVersion = VERSION_STRING;
    const QString jsScript = "window.onQtAppUpdate  && window.onQtAppUpdate(\"" + appVersion + "\", \"" + reference + "\", \"" + currentVersion + "\", \"" + message + "\");";
    LOG << "Update script " << jsScript;
    ui->webView->page()->runJavaScript(jsScript);
END_SLOT_WRAPPER
}

void MainWindow::softReloadPage() {
    LOG << "updateReady()";
    if (!isInitFinished) {
        std::lock_guard<std::mutex> lock(mutLastHtmls);
        last_htmls = Uploader::getLastHtmlVersion();
    } else {
        ui->webView->page()->runJavaScript("updateReady();");
    }
}

void MainWindow::softReloadApp() {
    QApplication::exit(RESTART_BROWSER);
}

void MainWindow::loadUrl(const QString &page) {
    LOG << "Reload. " << page << ".";
    shemeHandler->setLog();
    QUrl url(page);
    if (url.path().isEmpty())
        url.setPath("/");
    ui->webView->load(url);
    LOG << "Reload ok";
}

void MainWindow::loadFile(const QString &pageName) {
    loadUrl("file:///" + makePath(last_htmls.fullPath, pageName));
}

bool MainWindow::currentFileIsEqual(const QString &pageName) {
    return ui->webView->url().toString().endsWith(makePath(last_htmls.fullPath, pageName));
}

void MainWindow::addElementToHistoryAndCommandLine(const QString &text, bool isAddToHistory, bool isReplace) {
    LOG << "scl " << text;

    unregisterCommandLine();
    const QString currText = ui->commandLine->currentText();
    if (!currText.isEmpty()) {
        if (ui->commandLine->count() >= 1) {
            if (!pagesMappings.compareTwoPaths(ui->commandLine->itemText(ui->commandLine->count() - 1), currText)) {
                ui->commandLine->addItem(currText);
            }
        } else {
            ui->commandLine->addItem(currText);
        }
    }

    bool isSetText = true;
    if (pagesMappings.compareTwoPaths(currText, text)) {
        isSetText = isReplace;
    }
    if (isSetText) {
        ui->commandLine->setCurrentText(text);
    }

    currentTextCommandLine = text;

    if (isAddToHistory && isSetText) {
        if (historyPos == 0 || !pagesMappings.compareTwoPaths(history[historyPos - 1], text)) {
            history.insert(history.begin() + historyPos, text);
            historyPos++;

            ui->backButton->setEnabled(history.size() > 1);
        } else if (pagesMappings.compareTwoPaths(history[historyPos - 1], text)) {
            history.at(historyPos - 1) = text;
        }
    }

    registerCommandLine();
}

void MainWindow::onBrowserLoadFinished(const QUrl &url2) {
BEGIN_SLOT_WRAPPER
    const QString url = url2.toString();
    const auto found = pagesMappings.findName(url);
    if (found.has_value()) {
        LOG << "Set address after load " << found.value();
        addElementToHistoryAndCommandLine(found.value(), true, false);
    } else {
        if (!prevUrl.isNull() && !prevUrl.isEmpty() && url.startsWith(prevUrl)) {
            const QString request = url.mid(prevUrl.size());
            LOG << "Set address after load2 " << prevTextCommandLine << " " << request << " " << prevUrl;
            addElementToHistoryAndCommandLine(prevTextCommandLine + request, true, false);
        } else {
            LOG << "not set address after load " << url << " " << currentTextCommandLine;
        }
    }
    prevUrl = url;
    prevTextCommandLine = currentTextCommandLine;
END_SLOT_WRAPPER
}

void MainWindow::onSetCommandLineText(QString text) {
BEGIN_SLOT_WRAPPER
    addElementToHistoryAndCommandLine(text, true, true);
END_SLOT_WRAPPER
}

void MainWindow::onSetHasNativeToolbarVariable() {
BEGIN_SLOT_WRAPPER
    ui->webView->page()->runJavaScript("window.hasNativeToolbar = true;");
END_SLOT_WRAPPER
}

void MainWindow::setUserName(QString userName) {
    currentUserName = userName;
    LOG << "Set user name " << userName;
    ui->userButton->setText(userName);
    ui->userButton->adjustSize();

    auto *button = ui->userButton;
    const auto textSize = button->fontMetrics().size(button->font().style(), button->text());
    QStyleOptionButton opt;
    opt.initFrom(button);
    opt.rect.setSize(textSize);
    const int estimatedWidth = button->style()->sizeFromContents(QStyle::CT_ToolButton, &opt, textSize, button).width() + 25;
    button->setMaximumWidth(estimatedWidth);
    button->setMinimumWidth(estimatedWidth);
}

void MainWindow::onSetMappings(QString mapping) {
BEGIN_SLOT_WRAPPER
    LOG << PeriodicLog::make("s_mp") << "Set mappings " << mapping;
    pagesMappings.setMappings(mapping);
END_SLOT_WRAPPER
}

void MainWindow::onJsRun(QString jsString) {
BEGIN_SLOT_WRAPPER
    ui->webView->page()->runJavaScript(jsString);
END_SLOT_WRAPPER
}

void MainWindow::showExpanded() {
    show();
}

QString MainWindow::getServerIp(const QString &text) const {
    QString ip = pagesMappings.getIp(text);
    QUrl url(ip);
    return url.host();
}

LastHtmlVersion MainWindow::getCurrentHtmls() const {
    std::lock_guard<std::mutex> lock(mutLastHtmls);
    return last_htmls;
}

void MainWindow::onLogined(const QString &login) {
BEGIN_SLOT_WRAPPER
    if (login.isEmpty()) {
        if (isInitFinished) {
            LOG << "Try Swith to login";
            if (!currentFileIsEqual("login.html")) {
                LOG << "Swith to login";
                loadFile("login.html");
            }
        }
        setUserName(DEFAULT_USERNAME);
    } else {
        setUserName(login);
    }
END_SLOT_WRAPPER
}
