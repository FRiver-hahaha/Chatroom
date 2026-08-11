#include "MainWindow.h"
#include "widgets/ChatInputBar.h"
#include "widgets/ChatBubble.h"
#include "widgets/InfoPanel.h"
#include "widgets/FunctionBar.h"
#include "dialogs/AddFriendDialog.h"
#include "dialogs/FriendRequestDialog.h"
#include "dialogs/CreateGroupDialog.h"
#include "dialogs/JoinGroupDialog.h"
#include "dialogs/EditProfileDialog.h"
#include "dialogs/GameDialog.h"
#include "util/ConfettiOverlay.h"
#include "util/BombOverlay.h"
#include <QMenuBar>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QInputDialog>
#include <QFileDialog>
#include <QScrollBar>
#include <QRandomGenerator>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupConnections();
}

void MainWindow::setupUi() {
    setWindowTitle("ChatRoom");
    setMinimumSize(960, 640);
    resize(1100, 720);

    // menu bar
    auto *menuBar = this->menuBar();
    auto *momentsAction = menuBar->addAction("朋友圈");
    connect(momentsAction, &QAction::triggered, this, &MainWindow::momentsClicked);

    // central widget
    auto *central = new QWidget();
    setCentralWidget(central);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);

    main_splitter_ = new QSplitter(Qt::Horizontal);
    main_splitter_->setHandleWidth(1);

    // ===== left panel =====
    contact_model_ = new ContactListModel(this);
    contact_list_ = new QListView();
    contact_list_->setModel(contact_model_);
    contact_list_->setMinimumWidth(180);
    contact_list_->setStyleSheet(
        "QListView { background-color: #e6e6e6; border: none; }"
        "QListView::item { padding: 10px; border-bottom: 1px solid #d4d4d4; }"
        "QListView::item:selected { background-color: #c7c7c7; }");
    main_splitter_->addWidget(contact_list_);

    // ===== center panel =====
    auto *centerWidget = new QWidget();
    auto *centerLayout = new QVBoxLayout(centerWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    chat_title_ = new QLabel("选择一个联系人开始聊天");
    chat_title_->setStyleSheet(
        "font-size: 16px; font-weight: bold; padding: 10px; "
        "background-color: #f0f0f0; border-bottom: 1px solid #ddd;");
    chat_title_->setAlignment(Qt::AlignCenter);
    centerLayout->addWidget(chat_title_);

    message_model_ = new ChatMessageModel(this);
    message_list_ = new QListView();
    message_list_->setModel(message_model_);
    message_list_->setSelectionMode(QAbstractItemView::NoSelection);
    message_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    message_list_->setStyleSheet(
        "QListView { background-color: #f5f5f5; border: none; }");
    message_list_->setWordWrap(true);
    centerLayout->addWidget(message_list_, 1);

    input_bar_ = new ChatInputBar();
    centerLayout->addWidget(input_bar_);

    main_splitter_->addWidget(centerWidget);

    // ===== right panel =====
    right_stack_ = new QStackedWidget();
    right_stack_->setMinimumWidth(180);
    right_stack_->setMaximumWidth(250);

    info_panel_ = new InfoPanel();
    right_stack_->addWidget(info_panel_);

    function_bar_ = new FunctionBar();
    right_stack_->addWidget(function_bar_);

    right_stack_->setCurrentWidget(info_panel_);
    main_splitter_->addWidget(right_stack_);

    // set splitter proportions: 20:55:25
    main_splitter_->setStretchFactor(0, 2);
    main_splitter_->setStretchFactor(1, 5);
    main_splitter_->setStretchFactor(2, 2);

    centralLayout->addWidget(main_splitter_);

    // overlays
    confetti_ = new ConfettiOverlay(this);
    bomb_ = new BombOverlay(this);
}

void MainWindow::setupConnections() {
    auto *state = ClientState::instance();

    connect(contact_list_, &QListView::clicked, this, &MainWindow::onContactClicked);

    connect(input_bar_, &ChatInputBar::sendClicked, this, &MainWindow::onSendMessage);
    connect(input_bar_, &ChatInputBar::fileUploadClicked, this, &MainWindow::onFileUpload);
    connect(input_bar_, &ChatInputBar::gameClicked, this, &MainWindow::onGameClicked);
    connect(input_bar_, &ChatInputBar::loadHistoryClicked, this, &MainWindow::onLoadHistory);

    connect(state, &ClientState::contactsUpdated, this, &MainWindow::onContactsUpdated);
    connect(state, &ClientState::messagesUpdated, this, &MainWindow::onMessagesUpdated);
    connect(state, &ClientState::currentChatChanged, this, &MainWindow::onChatChanged);
    connect(state, &ClientState::systemNotification, this, &MainWindow::onSystemNotification);
    connect(state, &ClientState::incomingMessage, this, &MainWindow::onIncomingMessage);

    // function bar
    connect(function_bar_, &FunctionBar::addFriendClicked, this, &MainWindow::onAddFriend);
    connect(function_bar_, &FunctionBar::deleteFriendClicked, this, &MainWindow::onDeleteFriend);
    connect(function_bar_, &FunctionBar::blockFriendClicked, this, &MainWindow::onBlockFriend);
    connect(function_bar_, &FunctionBar::friendRequestsClicked, this, &MainWindow::onFriendRequests);
    connect(function_bar_, &FunctionBar::createGroupClicked, this, &MainWindow::onCreateGroup);
    connect(function_bar_, &FunctionBar::joinGroupClicked, this, &MainWindow::onJoinGroup);
    connect(function_bar_, &FunctionBar::groupInvitesClicked, this, &MainWindow::onGroupInvites);
    connect(function_bar_, &FunctionBar::editProfileClicked, this, &MainWindow::onEditProfile);
    connect(function_bar_, &FunctionBar::deleteAccountClicked, this, &MainWindow::onDeleteAccount);
    connect(function_bar_, &FunctionBar::quitGroupClicked, this, &MainWindow::onQuitGroup);
    connect(function_bar_, &FunctionBar::viewMembersClicked, this, &MainWindow::onViewMembers);
    connect(function_bar_, &FunctionBar::dismissGroupClicked, this, &MainWindow::onDismissGroup);
    connect(function_bar_, &FunctionBar::approveJoinClicked, this, &MainWindow::onApproveJoin);
    connect(function_bar_, &FunctionBar::removeMemberClicked, this, &MainWindow::onRemoveMember);
    connect(function_bar_, &FunctionBar::addAdminClicked, this, &MainWindow::onAddAdmin);
    connect(function_bar_, &FunctionBar::removeAdminClicked, this, &MainWindow::onRemoveAdmin);
}

void MainWindow::onLoginSuccess() {
    show();
}

// ===== left panel =====

void MainWindow::onContactClicked(const QModelIndex &index) {
    auto contacts = ClientState::instance()->contacts();
    if (index.row() < 0 || index.row() >= contacts.size()) return;

    const auto &c = contacts[index.row()];
    if (c.type == ContactItem::Friend) {
        ClientState::instance()->setCurrentChat(ChatType::Private, c.id);
        chat_title_->setText(c.name + (c.is_online ? " [在线]" : " [离线]"));
        updateRightPanel();
    } else {
        ClientState::instance()->setCurrentChat(ChatType::Group, 0, c.group_id);
        chat_title_->setText(c.name + " (群组)");
        function_bar_->setGroupMode(true, c.role);
        updateRightPanel();
    }
}

void MainWindow::onContactsUpdated() {
    auto *state = ClientState::instance();
    contact_model_->setContacts(state->contacts());
    if (state->currentChatType() == ChatType::Private) {
        const auto &c = state->contactById(state->currentTargetId(), ContactItem::Friend);
        if (c.id != 0)
            chat_title_->setText(c.name + (c.is_online ? " [在线]" : " [离线]"));
    }
}

void MainWindow::onMessagesUpdated() {
    message_model_->setMessages(ClientState::instance()->currentChatMessages());
    // scroll to bottom
    message_list_->scrollToBottom();
}

void MainWindow::onChatChanged() {
    onMessagesUpdated();
}

void MainWindow::onSystemNotification(const QString &text) {
    // show in status bar or as subtle notification
    statusBar()->showMessage(text, 5000);

    if (text.contains("上线了") || text.contains("下线了") || text.contains("离线")) {
        ClientState::instance()->queryFriends();
    }
}

void MainWindow::onIncomingMessage(uint64_t fromId, const QString &senderName, const QString &content) {
    auto *state = ClientState::instance();
    if (state->currentChatType() == ChatType::Private &&
        state->currentTargetId() != fromId) {
        statusBar()->showMessage(senderName + ": " + content.left(50), 3000);
    }
}

void MainWindow::updateRightPanel() {
    auto *state = ClientState::instance();
    auto contacts = state->contacts();
    for (const auto &c : contacts) {
        if (c.type == ContactItem::Friend && c.id == state->currentTargetId()) {
            info_panel_->showContactInfo(c);
            right_stack_->setCurrentWidget(info_panel_);
            return;
        }
        if (c.type == ContactItem::Group && c.group_id == state->currentGroupId()) {
            info_panel_->showContactInfo(c);
            right_stack_->setCurrentWidget(function_bar_);
            return;
        }
    }
}

// ===== chat =====

void MainWindow::onSendMessage(const QString &text) {
    auto *state = ClientState::instance();
    if (state->currentChatType() == ChatType::Private) {
        state->sendPrivateChat(state->currentTargetId(), text);
    } else {
        state->sendGroupChat(state->currentGroupId(), text);
    }
}

void MainWindow::onFileUpload() {
    auto *state = ClientState::instance();
    if (state->currentChatType() != ChatType::Private) {
        QMessageBox::information(this, "提示", "文件发送仅支持私聊");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this, "选择文件");
    if (filePath.isEmpty()) return;

    state->sendFileRequest(state->currentTargetId(), filePath);
}

void MainWindow::onGameClicked() {
    auto *dlg = new GameDialog(this);
    connect(dlg, &GameDialog::gameMove, this, &MainWindow::onGameMove);
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onLoadHistory() {
    auto *state = ClientState::instance();
    if (state->currentChatType() == ChatType::Private) {
        state->getHistory(state->currentTargetId(), 0);
    } else {
        state->getHistory(0, state->currentGroupId());
    }
}

void MainWindow::onGameMove(const QString &move) {
    // client-side rock-paper-scissors
    QStringList moves = {"rock", "scissors", "paper"};
    int ai = QRandomGenerator::global()->bounded(3);
    QString aiMove = moves[ai];

    // determine winner: rock > scissors, scissors > paper, paper > rock
    int playerIdx = moves.indexOf(move);
    bool playerWins = (playerIdx == 0 && ai == 1) ||  // rock vs scissors
                      (playerIdx == 1 && ai == 2) ||  // scissors vs paper
                      (playerIdx == 2 && ai == 0);    // paper vs rock
    bool tie = (playerIdx == ai);

    QString result;
    if (tie) {
        result = "平局!";
    } else if (playerWins) {
        result = "你赢了这一局!";
        confetti_->start();
    } else {
        result = "你输了这一局!";
        bomb_->start();
    }

    // if the dialog is still open, it will show result
    auto *dlg = qobject_cast<GameDialog *>(sender());
    if (dlg) {
        dlg->showResult(result, playerWins && !tie);
    }
}

// ===== function bar actions =====

void MainWindow::onAddFriend() {
    auto *dlg = new AddFriendDialog(this);
    if (dlg->exec() == QDialog::Accepted) {
        uint64_t uid = dlg->userId();
        if (uid > 0) {
            ClientState::instance()->addFriend(uid);
        } else if (!dlg->email().isEmpty()) {
            QMessageBox::information(this, "提示", "邮箱搜索需要服务端支持");
        }
    }
    dlg->deleteLater();
}

void MainWindow::onDeleteFriend() {
    auto *state = ClientState::instance();
    if (state->currentChatType() != ChatType::Private) return;

    auto ret = QMessageBox::question(this, "确认", "确定要删除该好友吗？将同时删除聊天记录。");
    if (ret == QMessageBox::Yes) {
        state->deleteFriend(state->currentTargetId());
    }
}

void MainWindow::onBlockFriend() {
    auto *state = ClientState::instance();
    if (state->currentChatType() != ChatType::Private) return;

    auto ret = QMessageBox::question(this, "确认", "确定要拉黑该好友吗？");
    if (ret == QMessageBox::Yes) {
        state->blockFriend(state->currentTargetId());
    }
}

void MainWindow::onFriendRequests() {
    auto *dlg = new FriendRequestDialog(this);
    // TODO: server needs to support fetching pending requests
    // For now show empty dialog, populate from system notifications
    connect(dlg, &FriendRequestDialog::approved, this, [](uint64_t userId) {
        ClientState::instance()->addFriend(userId);
    });
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onCreateGroup() {
    auto *dlg = new CreateGroupDialog(this);
    if (dlg->exec() == QDialog::Accepted) {
        ClientState::instance()->createGroup(
            dlg->groupName(), dlg->description(), dlg->isPublic());
    }
    dlg->deleteLater();
}

void MainWindow::onJoinGroup() {
    auto *dlg = new JoinGroupDialog(this);
    if (dlg->exec() == QDialog::Accepted && dlg->groupId() > 0) {
        ClientState::instance()->joinGroup(dlg->groupId());
    }
    dlg->deleteLater();
}

void MainWindow::onGroupInvites() {
    auto *dlg = new JoinGroupDialog(this);
    // TODO: load invites from server state
    connect(dlg, &JoinGroupDialog::acceptInvite, this, [](uint64_t groupId) {
        ClientState::instance()->joinGroup(groupId);
    });
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onEditProfile() {
    auto *dlg = new EditProfileDialog(this);
    if (dlg->exec() == QDialog::Accepted) {
        QString newNick = dlg->newNickname();
        if (!newNick.isEmpty()) {
            QMessageBox::information(this, "提示", "修改昵称需要服务端支持");
        }
    }
    dlg->deleteLater();
}

void MainWindow::onDeleteAccount() {
    bool ok;
    QString password = QInputDialog::getText(this, "注销账号",
        "请输入密码确认注销:", QLineEdit::Password, "", &ok);
    if (ok && !password.isEmpty()) {
        ClientState::instance()->deleteAccount(password);
    }
}

void MainWindow::onQuitGroup() {
    auto *state = ClientState::instance();
    state->quitGroup(state->currentGroupId());
}

void MainWindow::onViewMembers() {
    auto *state = ClientState::instance();
    state->queryGroupMembers(state->currentGroupId());
}

void MainWindow::onDismissGroup() {
    auto ret = QMessageBox::question(this, "确认", "确定要解散该群组吗？所有聊天记录将被删除。");
    if (ret == QMessageBox::Yes) {
        ClientState::instance()->dismissGroup(ClientState::instance()->currentGroupId());
    }
}

void MainWindow::onApproveJoin() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "批准加入", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->approveJoinGroup(
            ClientState::instance()->currentGroupId(), uid);
    }
}

void MainWindow::onRemoveMember() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "移除成员", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->removeGroupMember(
            ClientState::instance()->currentGroupId(), uid);
    }
}

void MainWindow::onAddAdmin() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "添加管理员", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->addGroupAdmin(
            ClientState::instance()->currentGroupId(), uid);
    }
}

void MainWindow::onRemoveAdmin() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "移除管理员", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->removeGroupAdmin(
            ClientState::instance()->currentGroupId(), uid);
    }
}
