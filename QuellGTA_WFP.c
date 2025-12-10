// 渟雲. Released to Public Domain.
//
// -----------------------------------------------------------------------------
// File: QuellGTA_WFP.c
// Author: 渟雲(quq[at]outlook.it)
// Date: 2025-12-5
//
// -----------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <fwpmu.h>
#include <fwptypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "fwpuclnt.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "rpcrt4.lib")

#define MAX_PATH_LENGTH 1024
#define TRANSACTION_TIMEOUT 5000

static const GUID kFirewallProviderGuid = {
    0x4310d91e,
    0xf336,
    0x43f3,
    {0xa2, 0x10, 0x88, 0x68, 0x48, 0xef, 0xc1, 0x4a}};

static const GUID kFirewallSublayerGuid = {
    0x9946b99e,
    0x15fb,
    0x4371,
    {0x9c, 0xa3, 0x84, 0xbe, 0x8b, 0x9b, 0x42, 0xad}};

typedef struct {
    char process_path[MAX_PATH_LENGTH];
    BOOL has_process;

    UINT32 remote_ip;
    UINT16 remote_port;
    UINT16 local_port;       // 新增：本地端口
    UINT8 ip_protocol;       // 新增：协议号 (6=TCP, 17=UDP)

    BOOL has_ip;
    BOOL has_remote_port;    // 改名为远程端口
    BOOL has_local_port;     // 新增：是否有本地端口
    BOOL has_protocol;       // 新增：是否有协议
} ConditionParams;

typedef enum {
  MODE_NONE = 0,
  MODE_CREATE,
  MODE_GLOBAL,
  MODE_CLEANUP
} OperationMode;

void PrintUsage(const char* program_name);
BOOL ParseArguments(int argc, char* argv[], OperationMode* mode,
                    ConditionParams* params);
BOOL InitializeWinsock();
BOOL RunAsAdministrator();
HANDLE OpenWfpEngine();
BOOL InitializeProvider(HANDLE engine_handle);
BOOL CleanupRules(HANDLE engine_handle);
BOOL CreateConditionalRule(HANDLE engine_handle, const ConditionParams* params);
void CloseWfpEngine(HANDLE engine_handle);
BOOL StartTransaction(HANDLE engine_handle);
BOOL CommitTransaction(HANDLE engine_handle);
BOOL AbortTransaction(HANDLE engine_handle);

int main(int argc, char* argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  OperationMode mode = MODE_NONE;
  ConditionParams params = {0};
  HANDLE engine_handle = NULL;
  BOOL success = FALSE;

  if (!RunAsAdministrator()) {
    return 1;
  }

  if (!ParseArguments(argc, argv, &mode, &params)) {
    PrintUsage(argv[0]);
    return 1;
  }

  if (!InitializeWinsock()) {
    return 1;
  }

  engine_handle = OpenWfpEngine();
  if (engine_handle == NULL) {
    WSACleanup();
    return 1;
  }

  switch (mode) {
    case MODE_CREATE:
      if (!InitializeProvider(engine_handle)) {
        break;
      }
      success = CreateConditionalRule(engine_handle, &params);
      break;

    case MODE_CLEANUP:
      success = CleanupRules(engine_handle);
      break;

    default:
      PrintUsage(argv[0]);
      success = FALSE;
      break;
  }

  CloseWfpEngine(engine_handle);
  WSACleanup();

  if (success) {
    return 0;
  } else {
    return 1;
  }
}

void PrintUsage(const char* program_name) {
    printf("\n");
    printf("=================================================================\n");
    printf("QuellGTA WFP 控制工具\n");
    printf("=================================================================\n");
    printf("\n");
    printf("基本模式:\n");
    printf("  -c, --create 创建条件阻断规则\n");
    printf("  -d, --delete 清理所有本工具创建的规则并恢复网络\n");
    printf("\n");
    printf("条件参数 (与 `-c` 模式配合使用，可任意组合):\n");
    printf("  按进程路径阻断 (必需完整路径，用双引号包裹)\n", "  -p, --process <路径>");
    printf("  按远程IPv4地址阻断 (如: 192.168.1.100)\n", "  -ip, --remote-ip <IP>");
    printf("  按远程端口阻断\n", "  -rp, --remote-port <端口>");
    printf("  按本地端口阻断\n", "  -lp, --local-port <端口>");
    printf("  按协议阻断 (tcp/udp 或 协议号)\n", "  -proto, --protocol <tcp|udp|数字>");
    printf("\n");
    printf("使用示例:\n");
    printf("    -c 创建全局规则\n", program_name);
    printf("    -d 清理所有本工具创建的规则并恢复网络\n");
    printf("  控制协议:\n");
    printf("    -c -p \"C:\\\\game.exe\" -proto tcp    # 仅阻断该进程的TCP连接\n");
    printf("    -c -p \"C:\\\\game.exe\" -proto udp    # 仅阻断该进程的UDP连接\n");
    printf("\n");
    printf("  控制本地端口:\n");
    printf("    -c -lp 27015                      # 阻断所有使用本地27015端口的连接\n");
    printf("    -c -p \"C:\\\\game.exe\" -lp 27015    # 阻断该进程使用本地27015端口的连接\n");
    printf("\n");
    printf("  控制远程端口:\n");
    printf("    -c -rp 443                        # 阻断所有到远程443端口(HTTPS)的连接\n");
    printf("    -c -p \"browser.exe\" -rp 443       # 阻断浏览器到HTTPS的连接\n");
    printf("\n");
    printf("  组合控制:\n");
    printf("    -c -rp 80 -lp 5000                # 阻断所有从本地5000到远程80端口的连接\n");
    printf("    -c -ip 192.168.1.1 -rp 53 -proto udp # 阻断所有到该IP UDP 53端口(DNS)的连接\n");
    printf("\n");
    printf("  精细控制 (完整示例):\n");
    printf("    -c -p \"C:\\\\game.exe\" -ip 1.2.3.4 -rp 27015 -lp 5000 -proto udp\n");
    printf("        # 效果: 仅阻断 game.exe 从本地5000端口(UDP)连接到 1.2.3.4:27015\n");
    printf("\n");
    printf("注意事项:\n");
    printf("  1. 所有条件为\"逻辑与\"关系，必须同时满足才会被阻断。\n");
    printf("  2. 清理规则(-d)后，已建立的TCP连接可能不会立即恢复，建议重启相关程序。\n");
    printf("  3. 使用`-p`参数时，请务必提供进程的完整绝对路径。\n");
    printf("  4. 本工具需要以管理员权限运行。\n");
    printf("=================================================================\n");
}

BOOL ParseArguments(int argc, char* argv[], OperationMode* mode,
                    ConditionParams* params) {
  if (argc < 2) {
    return FALSE;
  }

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--cteate") == 0) {
      *mode = MODE_CREATE;
    } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
      *mode = MODE_CLEANUP;
    } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--process") == 0) {
      if (i + 1 < argc) {
        strncpy_s(params->process_path, MAX_PATH_LENGTH, argv[i + 1],
                  _TRUNCATE);
        params->has_process = TRUE;
        i++;
      } else {
        return FALSE;
      }
    } else if (strcmp(argv[i], "-ip") == 0 || strcmp(argv[i], "--remote-ip") == 0) {
        if (i + 1 < argc) {
            // 解析IPv4地址
            struct in_addr addr;
            if (InetPtonA(AF_INET, argv[i + 1], &addr) == 1) {
                // 关键：这里存储的是网络字节序
                params->remote_ip = addr.S_un.S_addr; // 网络字节序
                params->has_ip = TRUE;

                // 【添加调试】打印原始值
                printf("[解析] IP原始值: %s -> 0x%08X\n",
                    argv[i + 1], params->remote_ip);
            }
            i++;
        }
    } else if (strcmp(argv[i], "-rp") == 0 || strcmp(argv[i], "--remote-port") == 0) {
        if (i + 1 < argc) {
            int port = atoi(argv[i + 1]);
            if (port > 0 && port <= 65535) {
                // 改为主机字节序（WFP期望主机字节序）
                params->remote_port = (UINT16)port;  // 去掉 htons()
                params->has_remote_port = TRUE;
            }
            i++;
        }
    }
    // 同理修改本地端口
    else if (strcmp(argv[i], "-lp") == 0 || strcmp(argv[i], "--local-port") == 0) {
        if (i + 1 < argc) {
            int port = atoi(argv[i + 1]);
            if (port > 0 && port <= 65535) {
                params->local_port = (UINT16)port;  // 去掉 htons()
                params->has_local_port = TRUE;
            }
            i++;
        }
    } else if (strcmp(argv[i], "-proto") == 0 || strcmp(argv[i], "--protocol") == 0) {    // 解析协议参数
        if (i + 1 < argc) {
            if (_stricmp(argv[i + 1], "tcp") == 0) {
                params->ip_protocol = 6;  // IPPROTO_TCP
                params->has_protocol = TRUE;
            }
            else if (_stricmp(argv[i + 1], "udp") == 0) {
                params->ip_protocol = 17; // IPPROTO_UDP
                params->has_protocol = TRUE;
            }
            else {
                // 也可以直接支持数字
                int proto = atoi(argv[i + 1]);
                if (proto > 0 && proto <= 255) {
                    params->ip_protocol = (UINT8)proto;
                    params->has_protocol = TRUE;
                }
            }
            i++;
        }
    } else {
      return FALSE;
    }
  }

  if (*mode == MODE_NONE) {
    return FALSE;
  }
  return TRUE;
}

BOOL InitializeWinsock() {
  WSADATA wsa_data;
  return (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0);
}

BOOL RunAsAdministrator() {
  BOOL is_admin = FALSE;
  PSID administrators_group = NULL;
  SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;

  if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &administrators_group)) {
    if (!CheckTokenMembership(NULL, administrators_group, &is_admin)) {
      is_admin = FALSE;
    }
    FreeSid(administrators_group);
  }

  return is_admin;
}

HANDLE OpenWfpEngine() {
  HANDLE engine_handle = NULL;
  FWPM_SESSION0 session = {0};
  DWORD status;

  session.displayData.name = L"QuellGTA WFP";
  session.displayData.description = L"WFP Control Tool for QuellGTA";
  session.flags = 0;
  session.txnWaitTimeoutInMSec = TRANSACTION_TIMEOUT;

  status =
      FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &engine_handle);

  if (status != ERROR_SUCCESS) {
    return NULL;
  }

  return engine_handle;
}

BOOL InitializeProvider(HANDLE engine_handle) {
  FWPM_PROVIDER0 provider = {0};
  FWPM_SUBLAYER0 sublayer = {0};
  DWORD status;

  FWPM_PROVIDER0* existing_provider = NULL;
  status = FwpmProviderGetByKey0(engine_handle, &kFirewallProviderGuid,
                                 &existing_provider);

  if (status == ERROR_SUCCESS && existing_provider != NULL) {
    FwpmFreeMemory0((void**)&existing_provider);
    return TRUE;
  }

  provider.displayData.name = L"MageAngela";
  provider.displayData.description = L"MageAngela WFP Provider";
  provider.providerKey = kFirewallProviderGuid;
  provider.flags = 0;

  status = FwpmProviderAdd0(engine_handle, &provider, NULL);
  if (status != ERROR_SUCCESS && status != FWP_E_ALREADY_EXISTS) {
    return FALSE;
  }

  sublayer.displayData.name = L"QuellGTA";
  sublayer.displayData.description = L"QuellGTA WFP Sublayer";
  sublayer.providerKey = &kFirewallProviderGuid;
  sublayer.subLayerKey = kFirewallSublayerGuid;
  sublayer.weight = 0xFFFF;

  status = FwpmSubLayerAdd0(engine_handle, &sublayer, NULL);
  if (status != ERROR_SUCCESS && status != FWP_E_ALREADY_EXISTS) {
    return FALSE;
  }

  return TRUE;
}

static BOOL CleanupRules(HANDLE engine_handle) {
    DWORD status = ERROR_SUCCESS;
    printf("[信息] 开始强制清理...\n");

    // 1. 开始事务
    status = FwpmTransactionBegin0(engine_handle, 0);
    if (status != ERROR_SUCCESS && status != FWP_E_TXN_IN_PROGRESS) {
        printf("[错误] 启动事务失败: 0x%08X\n", status);
        return FALSE;
    }
    BOOL transaction_begun = (status == ERROR_SUCCESS);
    printf("[信息] 事务状态: %s\n", transaction_begun ? "新建" : "加入现有");

    // 2. 精准枚举和删除过滤器
    HANDLE enum_handle = NULL;
    status = FwpmFilterCreateEnumHandle0(engine_handle, NULL, &enum_handle);
    if (status != ERROR_SUCCESS) {
        printf("[错误] 创建枚举句柄失败: 0x%08X\n", status);
        goto CLEANUP_ROLLBACK;
    }

    FWPM_FILTER0** filters = NULL;
    UINT32 num_filters = 0;
    UINT32 deleted_count = 0;

    // 循环分页枚举所有过滤器
    do {
        status = FwpmFilterEnum0(engine_handle, enum_handle, 512, &filters, &num_filters);
        if (status != ERROR_SUCCESS) {
            printf("[错误] 枚举过滤器失败: 0x%08X\n", status);
            break;
        }

        if (num_filters == 0) {
            FwpmFreeMemory0((void**)&filters);
            break;
        }

        printf("[信息] 本轮枚举到 %u 个过滤器，正在检查...\n", num_filters);

        for (UINT32 i = 0; i < num_filters; i++) {
            if (filters[i] == NULL) continue;

            // ========== 核心修正：使用 IsEqualGUID 进行安全比较 ==========
            BOOL match_provider = FALSE;
            BOOL match_sublayer = FALSE;
            BOOL match_display_name = FALSE;

            // 检查提供者密钥是否匹配 (providerKey 是指向 GUID 的指针)
            if (filters[i]->providerKey != NULL) {
                match_provider = IsEqualGUID(filters[i]->providerKey, &kFirewallProviderGuid);
            }
            // 检查子层密钥是否匹配 (subLayerKey 是 GUID 结构体)
            if (!match_provider) {
                match_sublayer = IsEqualGUID(&(filters[i]->subLayerKey), &kFirewallSublayerGuid);
            }
            // 检查显示名称是否包含特征字符串
            if (filters[i]->displayData.name != NULL) {
                match_display_name = (wcsstr(filters[i]->displayData.name, L"QuellGTA WFP") != NULL) ||
                    (wcsstr(filters[i]->displayData.name, L"QuellGTA Block") != NULL);
            }
            // ========== 修正结束 ==========

            if (match_provider || match_sublayer || match_display_name) {
                printf("[信息] 发现目标过滤器: %ws (提供者匹配:%d, 子层匹配:%d, 名称匹配:%d)\n",
                    filters[i]->displayData.name ? filters[i]->displayData.name : L"(无名)",
                    match_provider, match_sublayer, match_display_name);

                status = FwpmFilterDeleteByKey0(engine_handle, &filters[i]->filterKey);
                if (status == ERROR_SUCCESS) {
                    deleted_count++;
                    printf("[信息]   成功删除。\n");
                }
                else {
                    printf("[警告]   删除失败: 0x%08X\n", status);
                }
            }
        }

        FwpmFreeMemory0((void**)&filters);
        filters = NULL;
        num_filters = 0;

    } while (status == ERROR_SUCCESS);

    if (enum_handle) {
        FwpmFilterDestroyEnumHandle0(engine_handle, enum_handle);
    }

    printf("[信息] 共尝试删除 %u 个目标过滤器。\n", deleted_count);

    // 3. 再次尝试删除子层和提供者
    if (deleted_count > 0) {
        printf("[信息] 正在删除子层...\n");
        status = FwpmSubLayerDeleteByKey0(engine_handle, &kFirewallSublayerGuid);
        printf("[信息] 删除子层结果: 0x%08X\n", status);

        printf("[信息] 正在删除提供者...\n");
        status = FwpmProviderDeleteByKey0(engine_handle, &kFirewallProviderGuid);
        printf("[信息] 删除提供者结果: 0x%08X\n", status);
    }
    else {
        printf("[信息] 未发现目标过滤器，跳过删除子层和提供者。\n");
    }

    // 4. 提交事务
    if (transaction_begun) {
        status = FwpmTransactionCommit0(engine_handle);
        if (status == ERROR_SUCCESS) {
            printf("[成功] 清理事务已提交！\n");
            printf("[重要] 请务必重启所有被断网的程序，以清除其残留的TCP连接状态。\n");
            return TRUE;
        }
        else {
            printf("[错误] 提交事务失败: 0x%08X\n", status);
            goto CLEANUP_ROLLBACK;
        }
    }
    else {
        printf("[信息] 清理步骤已在外部事务中执行。\n");
        printf("[重要] 请务必重启所有被断网的程序。\n");
        return TRUE;
    }

CLEANUP_ROLLBACK:
    printf("[错误] 清理失败，正在回滚...\n");
    if (transaction_begun) {
        FwpmTransactionAbort0(engine_handle);
    }
    return FALSE;
}

BOOL StartTransaction(HANDLE engine_handle) {
  DWORD status = FwpmTransactionBegin0(engine_handle, 0);
  if (status != ERROR_SUCCESS && status != FWP_E_TXN_IN_PROGRESS) {
    return FALSE;
  }
  return (status == ERROR_SUCCESS);
}

BOOL CommitTransaction(HANDLE engine_handle) {
  DWORD status = FwpmTransactionCommit0(engine_handle);
  if (status != ERROR_SUCCESS) {
    return FALSE;
  }
  return TRUE;
}

BOOL AbortTransaction(HANDLE engine_handle) {
  FwpmTransactionAbort0(engine_handle);
  return TRUE;
}

void CloseWfpEngine(HANDLE engine_handle) {  // ========== 新增：CloseWfpEngine 函数定义 ==========
    if (engine_handle != NULL) {
        FwpmEngineClose0(engine_handle);
    }
}

BOOL CreateGlobalBlockRule(HANDLE engine_handle) {
  FWPM_FILTER0 filter = {0};
  DWORD status;
  BOOL in_transaction = FALSE;

  GUID filter_guid;
  (void)UuidCreate(&filter_guid);

  if (StartTransaction(engine_handle)) {
    in_transaction = TRUE;
  }

  filter.filterKey = filter_guid;
  filter.displayData.name = L"QuellGTA_Block";
  filter.displayData.description = L"Global block outbound IPv4 connections";

  filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
  filter.subLayerKey = kFirewallSublayerGuid;
  filter.weight.type = FWP_UINT8;
  filter.weight.uint8 = 0xF;

  filter.numFilterConditions = 0;
  filter.filterCondition = NULL;

  filter.action.type = FWP_ACTION_BLOCK;

  filter.providerKey = (GUID*)&kFirewallProviderGuid;
  filter.flags = FWPM_FILTER_FLAG_INDEXED | FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT;

  UINT64 filter_id;
  status = FwpmFilterAdd0(engine_handle, &filter, NULL, &filter_id);

  if (status != ERROR_SUCCESS) {
    if (in_transaction) {
      AbortTransaction(engine_handle);
    }
    return FALSE;
  }

  if (in_transaction) {
    if (!CommitTransaction(engine_handle)) {
      return FALSE;
    }
  }

  return TRUE;
}

BOOL CreateConditionalRule(HANDLE engine_handle, const ConditionParams* params) {
    printf("[调试] 输入参数：进程=%s, IP=%s, 远程端口=%u (原始值), 本地端口=%u, 协议指定=%d (值:%u)\n",
        params->has_process ? params->process_path : "(无)",
        params->has_ip ? "(已设置)" : "(无)",
        params->has_remote_port ? ntohs(params->remote_port) : 0,
        params->has_local_port ? ntohs(params->local_port) : 0,
        params->has_protocol, params->ip_protocol);

    FWPM_FILTER0 filter = { 0 };
    UINT32 condition_count = 0;
    DWORD status = ERROR_SUCCESS;
    FWP_BYTE_BLOB* app_blob_to_free = NULL;

    // ===== 动态构建条件（关键修改：使用独立静态变量）=====
    FWPM_FILTER_CONDITION condition_process = { 0 };
    FWPM_FILTER_CONDITION condition_ip = { 0 };
    FWPM_FILTER_CONDITION condition_remote_port = { 0 };
    FWPM_FILTER_CONDITION condition_local_port = { 0 };
    FWPM_FILTER_CONDITION condition_protocol = { 0 };

    FWPM_FILTER_CONDITION* all_conditions[5] = { 0 };

    // 条件1：进程
    if (params->has_process) {
        WCHAR wide_path[MAX_PATH_LENGTH];
        if (MultiByteToWideChar(CP_UTF8, 0, params->process_path, -1, wide_path, MAX_PATH_LENGTH) == 0) {
            return FALSE;
        }

        status = FwpmGetAppIdFromFileName0(wide_path, &app_blob_to_free);
        if (status != ERROR_SUCCESS || app_blob_to_free == NULL) {
            return FALSE;
        }

        condition_process.fieldKey = FWPM_CONDITION_ALE_APP_ID;
        condition_process.matchType = FWP_MATCH_EQUAL;
        condition_process.conditionValue.type = FWP_BYTE_BLOB_TYPE;
        condition_process.conditionValue.byteBlob = app_blob_to_free;

        all_conditions[condition_count] = &condition_process;
        condition_count++;
    }

    // 条件2：IP地址
    if (params->has_ip) {

        UINT32 ip_host_order = ntohl(params->remote_ip);

        condition_ip.fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
        condition_ip.matchType = FWP_MATCH_EQUAL;
        condition_ip.conditionValue.type = FWP_UINT32;

        condition_ip.conditionValue.uint32 = ip_host_order;

        printf("[调试] IP地址: 原始=0x%08X, 主机序=0x%08X\n",
            params->remote_ip, ip_host_order);

        all_conditions[condition_count] = &condition_ip;
        condition_count++;
    }

    // 条件3：远程端口
    if (params->has_remote_port) {
        condition_remote_port.fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
        condition_remote_port.matchType = FWP_MATCH_EQUAL;
        condition_remote_port.conditionValue.type = FWP_UINT16;
        condition_remote_port.conditionValue.uint16 = params->remote_port;

        all_conditions[condition_count] = &condition_remote_port;
        condition_count++;
    }

    // 条件4：本地端口
    if (params->has_local_port) {
        condition_local_port.fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
        condition_local_port.matchType = FWP_MATCH_EQUAL;
        condition_local_port.conditionValue.type = FWP_UINT16;
        condition_local_port.conditionValue.uint16 = params->local_port;

        all_conditions[condition_count] = &condition_local_port;
        condition_count++;
    }

    // 条件5：协议
    if (params->has_protocol) {
        condition_protocol.fieldKey = FWPM_CONDITION_IP_PROTOCOL;
        condition_protocol.matchType = FWP_MATCH_EQUAL;
        condition_protocol.conditionValue.type = FWP_UINT8;
        condition_protocol.conditionValue.uint8 = params->ip_protocol;

        all_conditions[condition_count] = &condition_protocol;
        condition_count++;
    }

    // 无任何条件则创建全局规则
    if (condition_count == 0) {
        printf("[信息] 未指定具体条件，创建全局阻断规则。\n");
        return CreateGlobalBlockRule(engine_handle);
    }

    // ===== 配置过滤器（采用测试成功的配置）=====
    UuidCreate(&filter.filterKey);
    filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    filter.displayData.name = L"QuellGTA Block";
    filter.displayData.description = L"WFP Block outbound IPv4 connections";
    filter.subLayerKey = kFirewallSublayerGuid;

    // 关键修改1：使用最高权重
    filter.weight.type = FWP_EMPTY;
    //filter.weight.uint8 = 0xF;

    // 关键修改2：直接使用条件数组
    filter.numFilterConditions = condition_count;
    // 需要将指针数组转换为FWPM_FILTER_CONDITION*数组
    // 这里我们使用一个临时数组来存储
    FWPM_FILTER_CONDITION final_conditions[5];
    for (UINT32 i = 0; i < condition_count; i++) {
        final_conditions[i] = *all_conditions[i];
    }
    filter.filterCondition = final_conditions;

    // 关键修改3：简化标志位
    filter.flags = FWPM_FILTER_FLAG_INDEXED;  // 只使用INDEXED

    filter.action.type = FWP_ACTION_BLOCK;
    filter.providerKey = (GUID*)&kFirewallProviderGuid;

    // ===== 添加过滤器 =====
    UINT64 filter_id = 0;
    status = FwpmFilterAdd0(engine_handle, &filter, NULL, &filter_id);

    // 资源清理
    if (app_blob_to_free != NULL) {
        FwpmFreeMemory0((void**)&app_blob_to_free);
    }

    if (status != ERROR_SUCCESS) {
        printf("[错误] 添加过滤器失败！状态码: 0x%08X\n", status);

        // 获取详细错误信息
        LPSTR msg_buf = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, NULL, status,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&msg_buf, 0, NULL);
        if (msg_buf) {
            printf("       错误描述: %s", msg_buf);
            LocalFree(msg_buf);
        }

        return FALSE;
    }

    printf("[成功] 过滤器创建成功！过滤器ID: %llu\n", filter_id);
    return TRUE;
}