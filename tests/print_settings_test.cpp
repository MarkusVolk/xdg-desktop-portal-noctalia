#include "print/print_settings.h"

#include <cmath>
#include <cstdio>
#include <print>

int main() {
  using namespace xdpn::print;
  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (!condition) {
      std::println(stderr, "FAIL: {}", what);
      ++failures;
    }
  };

  check(validRanges("1-3,5") && validRanges(" 2 ") && validRanges("4-4"), "valid ranges");
  check(!validRanges("") && !validRanges("0") && !validRanges("3-1") && !validRanges("1-2-3") && !validRanges("a"),
        "invalid ranges");
  check(rangesFromGtk("0-2,4") == "1-3,5" && rangesToGtk("1-3,5") == "0-2,4", "gtk ranges are 0-based");
  check(rangesToGtk("0").empty(), "page 0 does not exist");

  const auto a4 = pwgSizeMm("iso_a4_210x297mm");
  check(a4 && a4->first == 210 && a4->second == 297, "pwg mm");
  const auto letter = pwgSizeMm("na_letter_8.5x11in");
  check(letter && std::abs(letter->first - 215.9) < 0.01 && std::abs(letter->second - 279.4) < 0.01, "pwg inches");
  check(!pwgSizeMm("custom") && !pwgSizeMm("x_10x0mm"), "bad pwg names");

  GVariantBuilder settings;
  g_variant_builder_init(&settings, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&settings, "{sv}", "printer", g_variant_new_string("Pantum"));
  g_variant_builder_add(&settings, "{sv}", "n-copies", g_variant_new_string("2"));
  g_variant_builder_add(&settings, "{sv}", "collate", g_variant_new_string("false"));
  g_variant_builder_add(&settings, "{sv}", "print-pages", g_variant_new_string("ranges"));
  g_variant_builder_add(&settings, "{sv}", "page-ranges", g_variant_new_string("0-1,3"));
  g_variant_builder_add(&settings, "{sv}", "duplex", g_variant_new_string("vertical"));
  g_variant_builder_add(&settings, "{sv}", "use-color", g_variant_new_string("false"));
  GVariant* settingsValue = g_variant_ref_sink(g_variant_builder_end(&settings));
  GVariantBuilder page;
  g_variant_builder_init(&page, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&page, "{sv}", "Name", g_variant_new_string("iso_a5_148x210mm"));
  g_variant_builder_add(&page, "{sv}", "Orientation", g_variant_new_string("landscape"));
  GVariant* pageValue = g_variant_ref_sink(g_variant_builder_end(&page));

  const auto job = jobFromSettings(settingsValue, pageValue);
  check(job.printer == "Pantum" && job.copies == 2 && !job.collate, "printer, copies, collate");
  check(!job.allPages && job.ranges == "1-2,4", "ranges preset");
  check(job.sides == "two-sided-long-edge" && !job.color && job.landscape && job.media == "iso_a5_148x210mm",
        "sides, colour, orientation, media");

  const Media a5{"iso_a5_148x210mm", "A5", 148, 210, 5, 5, 5, 5};
  GVariant* back = g_variant_ref_sink(settingsFromJob(job, a5));
  GVariant* backPage = g_variant_ref_sink(pageSetupFromJob(job, a5));
  const auto again = jobFromSettings(back, backPage);
  check(again.printer == job.printer && again.copies == job.copies && again.collate == job.collate &&
            again.allPages == job.allPages && again.ranges == job.ranges && again.sides == job.sides &&
            again.color == job.color && again.landscape == job.landscape && again.media == job.media,
        "settings round trip");

  const auto options = cupsOptions(job);
  auto has = [&options](const char* key, const char* value) {
    for (const auto& [k, v] : options) {
      if (k == key && v == value) {
        return true;
      }
    }
    return false;
  };
  check(has("copies", "2") && has("page-ranges", "1-2,4") && has("sides", "two-sided-long-edge") &&
            has("print-color-mode", "monochrome") && has("media", "iso_a5_148x210mm") &&
            has("multiple-document-handling", "separate-documents-uncollated-copies"),
        "cups options");

  for (GVariant* value : {settingsValue, pageValue, back, backPage}) {
    g_variant_unref(value);
  }
  return failures == 0 ? 0 : 1;
}
