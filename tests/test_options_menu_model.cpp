// options_menu_model.hpp 的宿主机测试
#include "../exl/program/options_menu_model.hpp"

#include <cstdio>
#include <cstring>

using namespace sshud::options;

static int g_failures = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                 \
    }                                                               \
  } while (0)

static void testOnIndex() {
  CHECK(onIndexFromKeys("MOH_OFF", "MOH_ON").index == 1);
  CHECK(onIndexFromKeys("MOH_OFF", "MOH_ON").isCertain);
  CHECK(onIndexFromKeys("MOH_ON", "MOH_OFF").index == 0);
  CHECK(onIndexFromKeys("on", "off").index == 0);
  CHECK(onIndexFromKeys("ENABLED_ON", "DISABLED").index == 0);
  CHECK(onIndexFromKeys("DISABLED", "ENABLED_ON").index == 1);
  CHECK(!onIndexFromKeys("YES", "NO").isCertain);
  CHECK(onIndexFromKeys("YES", "NO").index == FALLBACK_ON_INDEX);
  CHECK(!onIndexFromKeys("A_OFF", "B_OFF").isCertain);
  CHECK(!onIndexFromKeys("", "").isCertain);
}

static void testLabels() {
  CHECK(isSimplifiedChinese(LANGUAGE_ZH));
  CHECK(isSimplifiedChinese(LANGUAGE_ZH_CN));
  CHECK(!isSimplifiedChinese(200));  // ZH_TW
  CHECK(!isSimplifiedChinese(44));   // EN
  CHECK(labelFor(0, LANGUAGE_ZH) == LABELS_ZH[0]);
  CHECK(labelFor(3, 44) == LABELS_EN[3]);
  CHECK(labelFor(CREST_ROW, LANGUAGE_ZH) == LABELS_ZH[ROW_COUNT - 1]);
}

static void testRowKeys() {
  CHECK(ROW_COUNT == sshud::toggles::FEATURE_COUNT + 1);
  CHECK(std::strcmp(rowKey(0), "enemy_bars") == 0);
  CHECK(std::strcmp(rowKey(sshud::toggles::FEATURE_COUNT - 1), "protect_delivery") == 0);
  CHECK(std::strcmp(rowKey(CREST_ROW), "start_crest") == 0);
}

static void testFitLayout() {
  const LayoutFit roomy = fitLayout(5, 766.0f, 50.0f, 105.0f);  // 原生 5 行：放得下
  CHECK(roomy.spacing == 105.0f && roomy.scale == 1.0f);
  const LayoutFit squeezed = fitLayout(8, 766.0f, 50.0f, 105.0f);  // 压行距就够
  CHECK(squeezed.spacing < 105.0f && squeezed.spacing >= 84.0f && squeezed.scale == 1.0f);
  const LayoutFit full = fitLayout(12, 766.0f, 50.0f, 105.0f);  // 实机：12 行
  CHECK(full.spacing == 84.0f);
  CHECK(full.scale > 0.76f && full.scale < 0.77f);
  CHECK(50.0f + 11.0f * full.spacing + LAYOUT_BOTTOM_MARGIN <= 766.0f / full.scale + 0.01f);
  CHECK(fitLayout(1, 766.0f, 50.0f, 105.0f).scale == 1.0f);
  CHECK(fitLayout(12, 0.0f, 50.0f, 105.0f).scale == 1.0f);
}

static void testAscii() {
  char out[8];
  asciiFromUtf16(u"MOH_ON", 6, out, sizeof(out));
  CHECK(std::strcmp(out, "MOH_ON") == 0);
  asciiFromUtf16(u"开ON", 3, out, sizeof(out));
  CHECK(std::strcmp(out, "?ON") == 0);
  asciiFromUtf16(u"ABCDEFGHIJ", 10, out, sizeof(out));
  CHECK(std::strcmp(out, "ABCDEFG") == 0);
  asciiFromUtf16(u"", 0, out, sizeof(out));
  CHECK(out[0] == '\0');
}

int main() {
  testOnIndex();
  testLabels();
  testRowKeys();
  testFitLayout();
  testAscii();
  if (g_failures == 0) std::printf("test_options_menu_model: all passed\n");
  return g_failures == 0 ? 0 : 1;
}
