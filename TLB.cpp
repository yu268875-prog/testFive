#include <iostream>
#include <vector>
#include <list>
#include <random>
#include <thread>
#include <chrono>
#include <iomanip>
#include <string>
#include <sstream>
#include <utility>

// ======================= 可调参数 =======================
constexpr int TOTAL_DATA = 128;                       // 数据总数
constexpr int NUM_TABLES = 4;                        // 页表数量
constexpr int PER_TABLE = TOTAL_DATA / NUM_TABLES;  // 每张页表项数 = 16
constexpr int TLB_SIZE = 16;                        // TLB 容量

constexpr int DELAY_MIN_MS = 600;                      // 冲突已解决                   // 页表访问最小延时(ms)

constexpr int DELAY_MAX_MS = 2000;                     // 页表访问最大延时(ms)
// =======================================================

struct PageTableEntry {
    bool valid = false;
    int  frame = -1;          // 物理帧号（对应 data[] 下标）
};

struct TLBEntry {
    int vaddr;                // 虚拟地址
    int frame;                // 物理帧号
};

class MMU {
public:
    MMU() : rng(std::random_device{}()), delayDist(DELAY_MIN_MS, DELAY_MAX_MS) {
        initMemory();
        initPageTables();
    }

    // 返回 <数据, 是否TLB命中>；地址越界时返回 {-1,false}
    std::pair<int, bool> access(int vaddr) {
        if (vaddr < 0 || vaddr >= TOTAL_DATA) {
            std::cout << "  [错误] 虚拟地址 " << vaddr
                << " 超出范围 [0, " << TOTAL_DATA - 1 << "]\n";
            return { -1, false };
        }

        std::cout << "\n[访问] 虚拟地址 " << vaddr
            << "  → 页表 " << (vaddr / PER_TABLE)
            << " , 表内偏移 " << (vaddr % PER_TABLE) << "\n";

        // ---------- 1. 查询 TLB ----------
        for (auto it = tlb.begin(); it != tlb.end(); ++it) {
            if (it->vaddr == vaddr) {
                TLBEntry e = *it;
                tlb.erase(it);
                tlb.push_front(e);                   // LRU：命中后移到队首
                ++hitCount;
                std::cout << "  [TLB 命中] → 物理帧 " << e.frame
                    << " , 数据 = " << data[e.frame] << "\n";
                return { data[e.frame], true };
            }
        }

        // ---------- 2. TLB 未命中 → 查页表 ----------
        ++missCount;
        std::cout << "  [TLB 未命中] 需要查询页表 ...\n";
        pageTableDelay();                            // 手动延时 1~2 秒

        int tbl = vaddr / PER_TABLE;
        int off = vaddr % PER_TABLE;
        const PageTableEntry& pte = pageTables[tbl][off];

        if (!pte.valid) {
            std::cout << "  [页错误] 页表 " << tbl << " 第 " << off
                << " 项无效，返回 -1\n";
            return { -1, false };
        }

        std::cout << "  [页表] 页表 " << tbl << " 第 " << off
            << " 项 → 物理帧 " << pte.frame
            << " , 数据 = " << data[pte.frame] << "\n";

        // ---------- 3. 回填 TLB ----------
        insertTLB(vaddr, pte.frame);

        return { data[pte.frame], false };
    }

    void printTLB() const {
        std::cout << "  TLB(最近→最旧): ";
        if (tlb.empty()) std::cout << "<空>";
        else for (const auto& e : tlb) std::cout << e.vaddr << " ";
        std::cout << "\n";
    }

    int hits()   const { return hitCount; }
    int misses() const { return missCount; }

private:
    std::vector<int> data;                              // 物理数据存储
    std::vector<std::vector<PageTableEntry>> pageTables; // 4 张页表
    std::list<TLBEntry> tlb;                            // LRU 链表
    std::mt19937 rng;
    std::uniform_int_distribution<int> delayDist;
    int hitCount = 0;
    int missCount = 0;

    void initMemory() {
        data.resize(TOTAL_DATA);
        for (int i = 0; i < TOTAL_DATA; ++i) data[i] = 100 + i;   // 数据: 100..163
    }

    void initPageTables() {
        pageTables.resize(NUM_TABLES);
        for (int t = 0; t < NUM_TABLES; ++t) {
            pageTables[t].resize(PER_TABLE);
            for (int o = 0; o < PER_TABLE; ++o) {
                int vaddr = t * PER_TABLE + o;
                pageTables[t][o].valid = true;
                pageTables[t][o].frame = vaddr;   // 简单一一映射
            }
        }
    }

    void insertTLB(int vaddr, int frame) {
        for (auto it = tlb.begin(); it != tlb.end(); ++it) {
            if (it->vaddr == vaddr) { tlb.erase(it); break; }
        }
        if (static_cast<int>(tlb.size()) >= TLB_SIZE) {
            std::cout << "  [TLB 淘汰] 移出 vaddr " << tlb.back().vaddr << "\n";
            tlb.pop_back();
        }
        tlb.push_front({ vaddr, frame });
        std::cout << "  [TLB 更新] 加入 vaddr " << vaddr
            << " → 帧 " << frame << "\n";
    }

    void pageTableDelay() {
        int ms = delayDist(rng);
        std::cout << "  [延时] 查询页表，等待 " << ms << " ms ...\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
};

// ======================= 主程序 =======================
int main() {
    std::cout << "==================================================\n";
    std::cout << "                TLB 查询模拟程序\n";
    std::cout << "--------------------------------------------------\n";
    std::cout << "  数据总数   : " << TOTAL_DATA << "\n";
    std::cout << "  页表数量   : " << NUM_TABLES
        << "（每张 " << PER_TABLE << " 项）\n";
    std::cout << "  TLB 容量   : " << TLB_SIZE << "\n";
    std::cout << "  页表延时   : " << DELAY_MIN_MS << " ~ " << DELAY_MAX_MS << " ms\n";
    std::cout << "--------------------------------------------------\n";
    for (int t = 0; t < NUM_TABLES; ++t) {
        std::cout << "  页表 " << t << " 负责虚拟地址 "
            << t * PER_TABLE << " ~ "
            << t * PER_TABLE + PER_TABLE - 1 << "\n";
    }
    std::cout << "==================================================\n";
    std::cout << "输入虚拟地址 [0~" << TOTAL_DATA - 1
        << "] 查询，输入 q 退出。\n";

    MMU mmu;
    int totalAccess = 0;

    while (true) {
        std::cout << "\n请输入虚拟地址 (0~" << TOTAL_DATA - 1 << ", q 退出): ";
        std::string line;
        if (!std::getline(std::cin, line)) break;   // EOF

        // 去掉首尾空白
        auto l = line.find_first_not_of(" \t");
        auto r = line.find_last_not_of(" \t");
        if (l == std::string::npos) { std::cout << "  (空输入，请重新输入)\n"; continue; }
        std::string token = line.substr(l, r - l + 1);

        if (token == "q" || token == "Q" || token == "quit") {
            break;
        }

        // 严格解析整数
        int vaddr;
        std::istringstream iss(token);
        if (!(iss >> vaddr) || !iss.eof()) {
            std::cout << "  [无效输入] \"" << token
                << "\" 不是有效整数，请重新输入\n";
            continue;
        }

        ++totalAccess;
        auto [val, hit] = mmu.access(vaddr);
        std::cout << "  >>> 得到数据: " << val << "\n";
        mmu.printTLB();
        std::cout << "--------------------------------------------------\n";
    }

    std::cout << "\n============ 统计 ============\n";
    std::cout << "有效访问次数 : " << (mmu.hits() + mmu.misses()) << "\n";
    std::cout << "TLB 命中     : " << mmu.hits() << "\n";
    std::cout << "TLB 未命中   : " << mmu.misses() << "\n";
    int valid = mmu.hits() + mmu.misses();
    if (valid > 0) {
        double rate = 100.0 * mmu.hits() / valid;
        std::cout << std::fixed << std::setprecision(1)
            << "命中率       : " << rate << "%\n";
    }
    std::cout << "（无效输入不计入统计）\n";
    return 0;
}