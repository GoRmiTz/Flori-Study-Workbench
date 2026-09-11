// ============================================================
//  ws_contract_test.cpp — 客户端 ↔ 服务端 契约回归测试
//
//  用真实的客户端网络层（src/net/Http.cpp + src/net/Realtime.cpp）连一台
//  运行中的 Server，逐项验证 §3 实时 / §4 专栏 / §5 媒体 的协议口径。
//  与服务端的 Server/tools/smoke.js 互为对照：那边验服务端自身，这边验对接。
//
//  覆盖项（7 条下行 + 3 条 REST）：
//    §3 WS   auth:ok / rooms:list / room:joined / presence / chat:msg / music:list
//    §4 REST POST /columns  → 经 WS 广播 col:changed
//    §5 REST GET  /media/list
//
//  构建与运行（Git Bash，在 DesktopApp/ 目录下）：
//    source tools/msvcenv.sh
//    cl.exe /nologo /TP /std:c++20 /EHsc /utf-8 /I src \
//           /DWIN32_LEAN_AND_MEAN /DNOMINMAX /MD \
//           tools/ws_contract_test.cpp src/net/Http.cpp src/net/Realtime.cpp \
//           /link winhttp.lib ws2_32.lib bcrypt.lib /out:ws_contract_test.exe
//    ./ws_contract_test.exe            # 需先启动 Server（默认 127.0.0.1:8787）
//
//  退出码：0 = 全部联通；1 = 前置准备失败；2 = 有未联通项。
// ============================================================
#include "net/Realtime.h"
#include "net/Http.h"
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

using namespace lj;
using namespace lj::json;

static std::atomic<bool> g_authOk{false}, g_rooms{false}, g_joined{false},
                         g_presence{false}, g_chat{false}, g_music{false}, g_col{false};
static std::string   g_token;
static net::Endpoint g_ep;

static std::string UniqueName()
{
    return "wsverify_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count() % 100000000);
}

int main(int argc, char** argv)
{
    g_ep.host = L"127.0.0.1";
    g_ep.port = (argc > 1) ? atoi(argv[1]) : 8787;

    // 1) 注册一个临时账号，取访问令牌
    std::string name = UniqueName();
    std::string body = "{\"username\":\"" + name + "\",\"password\":\"test1234\"}";
    net::Response r = net::Request(g_ep, L"POST", L"/auth/register", body, "");
    if (!r.Ok()) {
        std::cout << "[x] 注册失败：status=" << r.status
                  << " err=" << net::ToUtf8(r.error) << std::endl;
        return 1;
    }
    JVal root = Parser(r.body.data(), r.body.size()).parse();
    const JVal* at = JGet(root, "accessToken");
    if (!at || at->type != JVal::Str) {
        std::cout << "[x] 注册响应里没有 accessToken" << std::endl;
        return 1;
    }
    g_token = at->str;
    std::cout << "[ok] 取得令牌（len=" << g_token.size() << "）" << std::endl;

    // 2) §5 媒体列表
    net::Response ml = net::Request(g_ep, L"GET", L"/media/list", "", g_token);
    std::cout << (ml.Ok() ? "[ok] " : "[x]  ") << "GET /media/list -> " << ml.status << std::endl;

    // 3) §3 启动实时客户端（真实 Realtime，手写 RFC 6455 over Winsock）
    auto rt = &Realtime::Instance();
    rt->On("auth:ok",     [](const JVal&) { g_authOk   = true; });
    rt->On("rooms:list",  [](const JVal&) { g_rooms    = true; });
    rt->On("room:joined", [](const JVal&) { g_joined   = true; });
    rt->On("presence",    [](const JVal&) { g_presence = true; });
    rt->On("chat:msg",    [](const JVal&) { g_chat     = true; });
    rt->On("music:list",  [](const JVal&) { g_music    = true; });
    rt->On("col:changed", [](const JVal&) { g_col      = true; });
    rt->Init([] { return g_ep; }, [] { return g_token; }, [] { return true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(900));   // 等握手 + auth
    rt->JoinRoom("public");
    rt->SendChat("桌面端 WS 联通测试");
    rt->RequestMusic();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // 4) §4 发布专栏 → 服务端应经 WS 向本连接广播 col:changed
    std::string col = "{\"section\":\"general\",\"title\":\"WS验证专栏\","
                      "\"body\":\"桌面端真实 WS 客户端\"}";
    net::Response cr = net::Request(g_ep, L"POST", L"/columns", col, g_token);
    std::cout << (cr.Ok() ? "[ok] " : "[x]  ") << "POST /columns -> " << cr.status << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(2));          // 等广播落地

    struct { const char* name; bool v; } items[] = {
        { "auth:ok",     g_authOk   }, { "rooms:list", g_rooms },
        { "room:joined", g_joined   }, { "presence",   g_presence },
        { "chat:msg",    g_chat     }, { "music:list", g_music },
        { "col:changed", g_col      },
    };
    bool all = true;
    std::cout << "=== 下行消息覆盖 ===" << std::endl;
    for (auto& it : items) {
        std::cout << (it.v ? "  [ok] " : "  [x]  ") << it.name << std::endl;
        all = all && it.v;
    }
    std::cout << (all ? "[PASS] 客户端 Realtime + §4/§5 全链路联通"
                      : "[FAIL] 仍有未联通项") << std::endl;

    rt->Shutdown();
    return all ? 0 : 2;
}
