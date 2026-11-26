# Phase v2.1 详细规格：物理隔离凭据保险箱、双令牌轮换与账号生命周期体系

覆盖版本：`v2.1.0`（通道：`stable`，版本钩子升级：`minor`）。

---

## 1. 背景与现状痛点剖析

在 `v2.0.0` 及更早版本中，`sakura::net::APIClient` 通过 `LoadFromConfig()` / `SaveToConfig()` 将 `online.auth_token`、`online.user_id`、`online.username` 等凭据与游戏常规设置混存于 `config/settings.json`：

1. **设置重置与回滚误擦除登录态**：当玩家在 `SceneSettings` 中触发 `Config::ResetToDefaults()`、取消修改触发 `Config::Restore(m_saved)` 或导入外部配置备份时，内存根节点被整体覆盖，导致登录会话被静默清空。
2. **明文令牌泄露风险**：`online.auth_token` 以明文存储在 `settings.json` 中，玩家在反馈问题或分享键位配置时极易泄露在线身份凭据。
3. **单静态 JWT 缺乏续期与离线容错区分**：现有服务端仅签发单一 JWT，缺少可吊销的刷新令牌（Refresh Token）与滑动续期机制；客户端对「断网离线」与「令牌真正失效」缺乏精细状态区分。
4. **游客账号无法原地转正且联机大厅覆盖身份**：游客积累的本地/云端成绩、PP、Sakura Rating、段位徽章与谱面市场记录无法无损升级为正式注册账号；`SceneArena` 在未读取持久化会话或离线波动时可能误创建临时 `SakuraPlayer` 游客覆盖原身份。

`v2.1.0` 从客户端存储层、服务端认证协议与场景交互层彻底重构账号生命周期闭环。

---

## 2. 物理隔离凭据保险箱 (`src/core/auth_store.h` / `src/core/auth_store.cpp`)

### 2.1 模块归属与构建集成
- 新增 `src/core/auth_store.h` 与 `src/core/auth_store.cpp`，命名空间为 `sakura::core`。
- 将 `src/core/auth_store.cpp` 加入 `CMakeLists.txt` 的 `sakura-game-logic` 静态库（主程序 `Sakura` 通过 `GLOB_RECURSE` 自动收录），确保无 SDL3 窗口依赖的单元测试（含 Windows 与 `ci-linux`）可直接编译链接。
- 在 `CMakeLists.txt` 的 `if(WIN32)` 分支中为 `${PROJECT_NAME}` 与 `sakura-game-logic` 追加系统库链接 `crypt32`：
  ```cmake
  if(WIN32)
      target_link_libraries(sakura-game-logic PUBLIC mfplat mfreadwrite mfuuid ole32 winhttp crypt32)
  endif()
  ```

### 2.2 独立持久化路径与原子落盘
- **存储路径**：固定使用 `sakura::core::Paths::User("config/auth_session.json")`，与 `config/settings.json` 物理隔离。
- **原子写入**：所有落盘操作必须调用 `sakura::utils::AtomicWrite(path, jsonContent)`（先写临时文件并刷盘，再原子替换目标文件），杜绝进程异常退出或断电导致凭据文件损坏。
- **生命周期隔离保证**：
  - `Config::ResetToDefaults()`、`SceneSettings` 的 `Config::Restore(m_saved)` 以及设置备份导入导出严禁读写或清空 `auth_session.json`。
  - `settings.json` 仅保留非敏感端点偏好 `online.server_url`（默认 `https://steven-lee.tech:233`）。

### 2.3 Windows DPAPI 加密与 Linux CI 确定性混淆回退
- **令牌密文格式**：`access_token` 与 `refresh_token` 绝不以明文写入 `auth_session.json`，统一封装为带算法前缀的字符串：
  - **Windows (`#ifdef _WIN32`)**：调用 `<wincrypt.h>` / `<dpapi.h>` 中的 `CryptProtectData` 与 `CryptUnprotectData`：
    - 传入标志位 `CRYPTPROTECT_UI_FORBIDDEN`，禁止弹出任何系统交互对话框；
    - 绑定应用级附加熵常量 `kAuthEntropy = "Sakura::AuthStore::v2.1"`；
    - 输出格式为 `"dpapi:<base64>"`，绑定当前 Windows 登录用户上下文。若文件被复制到其他机器或用户下，`CryptUnprotectData` 安全返回失败（不崩溃），上层自动转入需重新验证状态。
  - **非 Windows 平台 (`#else`，面向 `ci-linux` 下的 `SakuraTests`)**：采用基于应用固定盐与 SHA-256 派生字节流的确定性混淆编码，输出格式为 `"obf:<base64>"`，保障 Linux 无头 CI 环境下序列化、反序列化、迁移与多账号切换单测 100% 编译通过。
  - **受损或未知前缀降级**：解密失败时记录 `spdlog::warn` 并返回空令牌，绝不抛出未捕获异常。

### 2.4 数据结构与多账号槽位设计
```cpp
namespace sakura::core
{

struct SavedAccountEntry
{
    std::string userId;
    std::string username;
    bool        isGuest               = true;
    double      totalPP               = 0.0;
    double      sakuraRating          = 0.0;
    std::string danBadge;
    std::string serverUrl;
    std::string encryptedAccessToken;  // dpapi:<base64> 或 obf:<base64>
    std::string encryptedRefreshToken; // dpapi:<base64> 或 obf:<base64>
    long long   expiresAt             = 0; // Access Token 过期 Unix 秒级时间戳
    long long   lastUsedAt            = 0; // MRU 排序时间戳
};

struct AuthSessionData
{
    std::string                    serverUrl = "https://steven-lee.tech:233";
    std::string                    activeUserId;
    std::string                    accessToken;  // 内存态解密后明文，落盘时加密
    std::string                    refreshToken; // 内存态解密后明文，落盘时加密
    long long                      expiresAt    = 0;
    SavedAccountEntry              profile;
    std::vector<SavedAccountEntry> savedAccounts; // 最多保留最近 5 个账号槽位
};

class AuthStore
{
public:
    static constexpr std::size_t MAX_SAVED_ACCOUNTS = 5;

    static AuthStore& GetInstance();
    explicit AuthStore(std::filesystem::path customStorePath = {});

    bool Load(Config* legacyConfigToMigrate = nullptr);
    bool Save() const;

    bool HasActiveSession() const;
    AuthSessionData GetSession() const;

    void UpsertActiveSession(const AuthSessionData& session);
    void UpdateProfileMetadata(double totalPP, double sakuraRating, const std::string& danBadge);
    void ClearActiveTokensKeepIdentity(); // 用于 SessionExpired：清空令牌但保留账号名供重登
    void ClearActiveSession(bool removeFromSavedSlots = false);

    std::vector<SavedAccountEntry> GetSavedAccounts() const;
    bool SwitchToSavedAccount(const std::string& userId, AuthSessionData* outSession = nullptr);
    bool RemoveSavedAccount(const std::string& userId);

    static std::string ProtectSecret(std::string_view plaintext);
    static std::optional<std::string> UnprotectSecret(std::string_view protectedText);
};

} // namespace sakura::core
```
- **最近 5 账号一键切换（MRU 槽位）**：
  - `saved_accounts` 数组按 `lastUsedAt` 降序维护最多 `5` 个 `SavedAccountEntry`。
  - **严禁存储任何明文或可逆密码**：槽位仅保存加密后的 `refresh_token` / `access_token` 与展示元数据（用户名、是否游客、Sakura Rating、段位徽章、所属 `serverUrl`）。
  - 玩家点击已保存槽位时，直接激活对应 `refresh_token`（若 Access Token 临期则静默刷新），实现免输密码一键切换大号/小号/测试服账号；若该槽位刷新令牌已被吊销，则弹出密码框并预填 `username`。

### 2.5 旧版 `settings.json` 自动一次性迁移
- 启动执行 `AuthStore::Load(&Config::GetInstance())` 时：
  1. 若 `auth_session.json` 尚不存在（或无有效会话），检查 `Config` 中是否存在非空的 `online.auth_token` 与 `online.user_id`。
  2. 若存在，提取 `online.user_id`、`online.username`、`online.is_guest`、`online.total_pp`、`online.sakura_rating`、`online.dan_badge`、`online.server_url` 及 `online.auth_token`，加密写入 `auth_session.json` 并生成首个 `SavedAccountEntry`。
  3. 迁移落盘成功后，立即调用 `cfg.Remove("online.auth_token")`（同时清理冗余凭据字段，仅保留 `online.server_url`）并 `cfg.SaveForce()`，彻底擦除 `settings.json` 中的历史明文令牌。

---

## 3. 服务端双令牌轮换与游客无损转正 (`server/`)

### 3.1 `auth_sessions` 幂等数据表 (`server/src/storage/sqlite_adapter.ts`)
在 SQLite 初始化迁移中新增幂等建表与索引语句（已有库无缝升级，不破坏现有 `users`、`scores`、`market_charts` 数据）：
```sql
CREATE TABLE IF NOT EXISTS auth_sessions (
    id                 TEXT PRIMARY KEY,
    user_id            TEXT NOT NULL,
    refresh_token_hash TEXT NOT NULL UNIQUE,
    device_name        TEXT NOT NULL DEFAULT '',
    created_at         INTEGER NOT NULL,
    expires_at         INTEGER NOT NULL,
    revoked_at         INTEGER DEFAULT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_auth_sessions_user_id ON auth_sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_auth_sessions_refresh_hash ON auth_sessions(refresh_token_hash);
```
- **安全约束**：数据库仅存储 `refresh_token` 的 SHA-256 十六进制摘要（`refresh_token_hash`），绝不落盘原始 `refresh_token`。

### 3.2 双令牌签发规范 (`server/src/auth.ts` & `server/src/app.ts`)
- **令牌有效期**：
  - `token`（Access Token）：HS256 JWT，有效期 **7 天**（`7 * 86400` 秒），Payload 保持 `{ sub, username, guest, exp }`。
  - `refresh_token`（Refresh Token）：32 字节 CSPRNG 随机串（`crypto.randomBytes(32).toString("base64url")`），有效期 **90 天**（`90 * 86400` 秒）。
- **向后兼容签发**：
  - `POST /api/v1/auth/guest`、`POST /api/v1/auth/register`、`POST /api/v1/auth/login` 除原有 `ok`、`token`、`user` 字段外，统一新增返回 `refresh_token`（字符串）与 `expires_at`（Access Token 到期秒级时间戳），可选接收请求体字段 `device_name`（如 `"Windows Desktop"`）。
  - 旧版客户端仅读取 `token` 与 `user`，完全向后兼容。

### 3.3 新增认证与账号管理端点

| 端点 | 鉴权要求 | 请求体 | 核心处理逻辑与响应规格 |
|---|---|---|---|
| `POST /api/v1/auth/refresh` | 无需 Bearer（凭 `refresh_token`） | `{ "refresh_token": "...", "device_name"?: "..." }` | 校验 `refresh_token_hash` 存在、`revoked_at IS NULL` 且 `expires_at > now`。在单个 SQLite 事务内将旧记录标记 `revoked_at = now`（滑动轮换防重放），签发全新 7 天 `token`（读取 `users` 表最新 `username` 与 `is_guest`）+ 全新 90 天 `refresh_token` + `expires_at` + 最新 `user` 档案。若无效或已过期/已吊销，返回 HTTP `401`（`{ "ok": false, "code": "session_expired", "error": "..." }`）。 |
| `POST /api/v1/auth/logout` | 可选 Bearer | `{ "refresh_token"?: "..." }` | 幂等标记匹配的 `auth_sessions.revoked_at = now`，返回 `{ "ok": true }`（即使令牌本就不存在或已吊销也返回 `200 OK`）。 |
| `POST /api/v1/auth/upgrade-guest` | 必须携带当前游客 Bearer JWT | `{ "username": "...", "password": "..." }` | 校验当前 `user.is_guest === true`；校验新 `username` 格式合法（`3~24` 字符，大小写不敏感唯一，冲突返回 `409`）且 `password` 长度 `>= 6`。**在同一 `user_id` 上原地执行 `UPDATE users SET username = ?, password_hash = ?, is_guest = 0 WHERE id = ?`**（密码使用 `auth.ts` 既有 `scrypt` `hashPassword`），同步更新其已发布 `market_charts.uploader_name`。100% 原地保留该 `user_id` 名下全部 `scores`、`daily_scores`、`course_records`、`achievements`、`total_pp`、`sakura_rating`、`dan_badge` 以及谱面市场上传与评分记录。随后签发 `guest: false` 的新 `token`、`refresh_token`、`expires_at` 与更新后的 `user`。 |
| `POST /api/v1/auth/change-password` | 必须携带正式账号 Bearer JWT | `{ "old_password": "...", "new_password": "...", "refresh_token"?: "..." }` | 拒绝游客调用（返回 `400`）；使用 `verifyPassword` 校验 `old_password`（不匹配返回 `401`）；校验 `new_password` 长度 `>= 6`；更新 `users.password_hash = hashPassword(new_password)`，吊销该 `user_id` 下除当前设备会话外的全部其余 `auth_sessions`，并轮换返回当前会话新 `token`、`refresh_token`、`expires_at`。 |

---

## 4. 客户端 `APIClient` 认证状态机与场景集成

### 4.1 四态认证枚举与断网保活 (`src/net/api_client.h`)
在 `sakura::net` 命名空间新增明确区分「网络可达性」与「凭据有效性」的枚举：
```cpp
enum class AuthState
{
    Unauthenticated,      // 本地无已登录账号
    AuthenticatedOnline,  // 凭据有效且已与服务端连通确认
    AuthenticatedOffline, // 本地持有有效身份与令牌，但当前网络不可达/超时/5xx（保活本地身份）
    SessionExpired        // 服务端明确拒绝刷新令牌（HTTP 401/403），需重新输入密码登录
};
```
- **核心状态流转铁律**：
  1. 客户端启动调用 `APIClient::LoadFromConfig()` 时，内部委托 `AuthStore::Load(&Config::GetInstance())` 加载会话：若存在有效令牌，初始设为 `AuthenticatedOffline` 并在后台发起 `RefreshProfileAsync()`（若 `expiresAt` 临近 24 小时内或已过期则先走 `RefreshSession()`）；连通成功后升为 `AuthenticatedOnline`。
  2. **任何网络层失败（`HttpResponse::IsNetworkError()`，即 `statusCode == 0`、DNS 解析失败、连接超时、TLS 握手中断）或服务端临时错误（`5xx`）**：状态转为 `AuthenticatedOffline`，**严禁清空 `AuthStore` 中的令牌与 `OnlineUser` 档案**。玩家在主菜单、选歌页与个人统计页依然能看到自己的用户名、`danBadge` 与 `sakuraRating`，打歌成绩自动进入离线队列。
  3. **HTTP `401 Unauthorized` 单飞静默刷新与透明重试（Single-Flight Refresh）**：
     - 当任意带鉴权的 API 请求收到 HTTP `401` 时，进入由 `m_refreshMutex` + `m_refreshCv` 保护的 `RefreshSessionSingleFlight()`：
     - 若已有其他工作线程正在刷新，当前线程等待其结果并直接复用新签发的 `access_token`（避免并发请求同时触发多次 `/auth/refresh` 导致轮换冲突）；
     - 若刷新成功（`200 OK`）：原子更新内存与 `AuthStore`，置为 `AuthenticatedOnline`，并使用新 `Authorization: Bearer <token>` **透明重放原请求一次**，上层业务调用方完全无感；
     - 若刷新请求本身遭遇网络错误或 `5xx`：置为 `AuthenticatedOffline`，保留 `refresh_token` 待网络恢复后重试；
     - **仅当 `/api/v1/auth/refresh` 明确返回 HTTP `401` 或 `403`（或本地无 `refresh_token` 且原请求 `401`）时**：调用 `AuthStore::ClearActiveTokensKeepIdentity()`，将状态切换为 `AuthState::SessionExpired`。

### 4.2 `APIClient` 新增公开接口
```cpp
AuthState GetAuthState() const;
AuthResult RefreshSession();
AuthResult UpgradeGuest(const std::string& username, const std::string& password);
AuthResult ChangePassword(const std::string& oldPassword, const std::string& newPassword);
std::vector<sakura::core::SavedAccountEntry> GetSavedAccounts() const;
AuthResult SwitchSavedAccount(const std::string& userId);
bool RemoveSavedAccount(const std::string& userId);

void UpgradeGuestAsync(const std::string& username, const std::string& password,
                       std::function<void(AuthResult)> cb);
void ChangePasswordAsync(const std::string& oldPassword, const std::string& newPassword,
                         std::function<void(AuthResult)> cb);
void SwitchSavedAccountAsync(const std::string& userId, std::function<void(AuthResult)> cb);
void LogoutAsync(std::function<void()> cb = {});
```
- `Logout()` / `LogoutAsync()`：向服务端异步发送 `POST /api/v1/auth/logout` 吊销当前 `refresh_token`，清除当前激活会话（可保留槽位元信息但清除其令牌，或按用户选择移除槽位），状态置为 `AuthState::Unauthenticated`。

### 4.3 `SceneMenu` 与 `SceneArena` 交互升级
- **`SceneMenu` 在线账号弹窗升级 (`src/scene/scene_menu.*`)**（全部布局严格遵循 `0.0~1.0` 归一化相对比例）：
  - 账号胶囊实时展示四态视觉指示（翠绿在线点 / 琥珀离线保活点 / 绯红过期感叹号 / 灰白未登录）以及 `[段位徽章] 用户名 · ★ Rating`。
  - **游客一键转正视图**：当 `user.isGuest == true` 时显示高亮「升级为正式账号（保留全部战绩与 Rating）」入口，输入期望用户名与密码即可调用 `UpgradeGuestAsync` 原地转正。
  - **已保存账号快捷切换栏**：展示 `AuthStore::GetSavedAccounts()` 中的最近 5 个槽位卡片（标注正式/游客标签与 Rating），支持一键点击切换或点击 `×` 移除本地记录。
  - **修改密码子面板**：正式账号已登录态下提供「修改密码」表单，校验旧密码并提交新密码。
  - **会话过期重登引导**：当 `GetAuthState() == AuthState::SessionExpired` 时，自动预填原 `username` 并提示「登录凭据已过期，请重新输入密码验证」。
- **`SceneArena` 持久化身份复用修复 (`src/scene/scene_arena.*`)**：
  - 移除进入多人大厅时无条件新建临时 `SakuraPlayer` 游客的逻辑。
  - `SceneArena::OnEnter()` 优先检查 `APIClient::GetAuthState()`：
    - 若已处于 `AuthenticatedOnline` 或 `AuthenticatedOffline`，直接复用持久化账号（若令牌临期或处于离线态则先尝试一次 `RefreshSession()` / `RefreshProfile()` 恢复在线连接）；
    - 若处于 `SessionExpired`，提示玩家返回验证密码或显式确认切换游客；
    - 仅当 `Unauthenticated`（本地从未登录过任何账号）且玩家确认以游客进入时，才调用 `LoginGuestAsync` 创建并持久化单一游客账号。

---

## 5. 测试矩阵与验收清单

### 5.1 C++ 单元测试 (`tests/test_v210_auth_persistence.cpp`)
在 `tests/CMakeLists.txt` 注册进 `SakuraTests`，覆盖以下必测断言：
1. **密文存取与防篡改**：验证 `AuthStore::ProtectSecret` / `UnprotectSecret` 在当前平台往返一致，`auth_session.json` 原始文本不包含明文 `access_token` 或 `refresh_token`，损坏密文优雅降级不崩溃。
2. **物理隔离性验证**：写入会话后依次执行 `Config::ResetToDefaults()` 与 `Config::Restore(snapshot)`，断言 `AuthStore::GetSession()` 与 `APIClient::GetCurrentUser()` 完全不受影响。
3. **旧版 `settings.json` 自动迁移**：构造含明文 `online.auth_token` 的旧版 `settings.json`，调用 `APIClient::LoadFromConfig()`，断言会话成功迁入 `auth_session.json` 且 `settings.json` 中 `online.auth_token` 已被彻底移除。
4. **5 槽位 MRU 轮转与切换**：连续登录 6 个不同账号，断言仅保留最近使用的 5 个槽位，无任何密码字段落盘，且 `SwitchSavedAccount` 正确恢复对应令牌与档案。
5. **断网保活与 401 单飞刷新重试**：基于 `MockHttpTransport`：
   - 模拟 `statusCode = 0` 网络故障，断言 `GetAuthState() == AuthState::AuthenticatedOffline` 且本地用户名、Rating、段位徽章完整保留；
   - 模拟业务接口首次返回 `401`、`/api/v1/auth/refresh` 返回新双令牌、重放业务接口返回 `200 OK`，断言业务调用最终成功且并发 `401` 仅触发一次 `/auth/refresh`；
   - 模拟 `/api/v1/auth/refresh` 返回 `401`，断言状态精准切换至 `AuthState::SessionExpired`。

### 5.2 服务端测试与联机端到端校验
- **`server/tests/server.test.ts`**：新增双令牌签发、`/auth/refresh` 滑动轮换与旧刷新令牌重放拒绝、`/auth/logout` 吊销、`/auth/upgrade-guest` 原地转正后 100% 保留历史成绩/PP/Sakura Rating/段位/市场谱面归属与评分、用户名冲突 `409`、`/auth/change-password` 密码校验及跨设备会话清理测试。
- **`Sakura.exe --verify-online`**：新增在线闭环自检步骤（游客建号 → 提交成绩获取 PP/Rating → `/auth/upgrade-guest` 原地转正 → 核对 `user_id` 与历史最高分无损保留 → `/auth/refresh` 轮换 → `/auth/change-password` → `/auth/logout`）。
