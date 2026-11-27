// 单元测试：首次建库的目录勾选逻辑
//
// 这部分以前无法测 —— 逻辑写在 borealis 的点击回调里，
// 只能断言「界面上有个方块」。抽成纯函数后，两条真实边界可以被钉住：
//
//   1. 默认全选（存排除项而不是选中项）
//   2. 探测结果变化后清洗旧排除项（拔卡再插回应重新默认选中）

#include "model/setup_selection.hpp"
#include "test_framework.hpp"

#include <set>
#include <string>
#include <vector>

using namespace muvik;

int main() {
    TEST_SUITE();

    TEST_BEGIN("默认全选：空排除集合意味着全部勾选");
    {
        std::set<std::string> excluded;
        const std::vector<std::string> detected = {
            "/home/u/Videos", "/home/u/Movies", "/Volumes/Card"};
        CHECK_EQ(countSelected(excluded, detected), (size_t)3, "三个目录默认全选");
        CHECK_TRUE(isSelected(excluded, "/home/u/Videos"), "单个目录默认被选中");
    }

    TEST_BEGIN("切换：按一次取消，再按一次恢复");
    {
        std::set<std::string> excluded;
        const std::string p = "/home/u/Movies";
        const std::vector<std::string> detected = {"/home/u/Videos", p};

        toggleExcluded(excluded, p);
        CHECK_EQ(excluded.size(), (size_t)1, "取消后出现在排除集合里");
        CHECK_TRUE(!isSelected(excluded, p), "该目录不再被勾选");
        CHECK_EQ(countSelected(excluded, detected), (size_t)1, "选中数降为 1");

        toggleExcluded(excluded, p);
        CHECK_EQ(excluded.size(), (size_t)0, "再按一次移出排除集合");
        CHECK_TRUE(isSelected(excluded, p), "该目录恢复选中");
        CHECK_EQ(countSelected(excluded, detected), (size_t)2, "选中数回到 2");
    }

    TEST_BEGIN("切换：空路径不进入排除集合");
    {
        std::set<std::string> excluded;
        toggleExcluded(excluded, "");
        CHECK_EQ(excluded.size(), (size_t)0, "空路径被忽略");
    }

    TEST_BEGIN("清洗：不再存在的排除项要被丢掉（拔卡再插回）");
    {
        std::set<std::string> excluded;

        // 第一次探测：有 SD 卡，用户把它取消勾选了
        const std::vector<std::string> first = {"/home/u/Videos", "/Volumes/Card"};
        toggleExcluded(excluded, "/Volumes/Card");
        CHECK_EQ(countSelected(excluded, first), (size_t)1, "SD 卡被排除");

        // 第二次探测：卡拔了，探测不到它 —— 排除项应被清掉
        const std::vector<std::string> second = {"/home/u/Videos"};
        const size_t removed = pruneExcluded(excluded, second);
        CHECK_EQ(removed, (size_t)1, "清掉了一条失效的排除记录");
        CHECK_EQ(excluded.size(), (size_t)0, "排除集合已清空");

        // 第三次探测：卡插回来了 —— 必须重新默认选中，
        // 否则用户看到「明明插着卡却导不进去」
        const std::vector<std::string> third = {"/home/u/Videos", "/Volumes/Card"};
        CHECK_TRUE(isSelected(excluded, "/Volumes/Card"), "插回的卡恢复默认选中");
        CHECK_EQ(countSelected(excluded, third), (size_t)2, "两个目录都被选中");
    }

    TEST_BEGIN("清洗：仍然存在的排除项必须保留");
    {
        std::set<std::string> excluded;
        const std::vector<std::string> detected = {"/a", "/b", "/c"};
        toggleExcluded(excluded, "/b");

        const size_t removed = pruneExcluded(excluded, detected);
        CHECK_EQ(removed, (size_t)0, "没有失效项");
        CHECK_EQ(excluded.size(), (size_t)1, "排除项仍在");
        CHECK_TRUE(!isSelected(excluded, "/b"), "/b 仍未被选中");
        CHECK_EQ(countSelected(excluded, detected), (size_t)2, "选中数仍为 2");
    }

    TEST_BEGIN("全部取消：选中数为 0");
    {
        std::set<std::string> excluded;
        const std::vector<std::string> detected = {"/a", "/b"};
        toggleExcluded(excluded, "/a");
        toggleExcluded(excluded, "/b");
        CHECK_EQ(countSelected(excluded, detected), (size_t)0, "一个都没选");
    }

    TEST_SUMMARY();
}
