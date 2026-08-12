set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(QT_CLIENT_SOURCES
    src/main.cpp
    src/network/ProtocolClient.cpp
    src/network/FrameDecoder.cpp
    src/models/ChatMessageModel.cpp
    src/models/ContactListModel.cpp
    src/models/GroupMemberModel.cpp
    src/state/ClientState.cpp
    src/state/MessageStore.cpp
    src/ui/LoginWindow.cpp
    src/ui/MainWindow.cpp
    src/ui/widgets/ChatBubble.cpp
    src/ui/widgets/ChatMessageDelegate.cpp
    src/ui/widgets/ChatInputBar.cpp
    src/ui/widgets/InfoPanel.cpp
    src/ui/widgets/FunctionBar.cpp
    src/ui/dialogs/AddFriendDialog.cpp
    src/ui/dialogs/FriendRequestDialog.cpp
    src/ui/dialogs/CreateGroupDialog.cpp
    src/ui/dialogs/JoinGroupDialog.cpp
    src/ui/dialogs/EditProfileDialog.cpp
    ${PROTO_SRCS}
)

add_executable(qt-client ${QT_CLIENT_SOURCES})

target_include_directories(qt-client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/qt-client/src
    ${PROTO_GEN_DIR}
    ${CMAKE_BINARY_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(qt-client PRIVATE
    ${PROTOBUF_LIBRARIES}
    OpenSSL::Crypto
    Threads::Threads
    Qt6::Widgets
    Qt6::Network
    Qt6::Core
    Qt6::Sql
)

target_compile_options(qt-client PRIVATE
    -Wall
    -Wextra
)
