#include "print/print.h"

#include "dbus/introspection.h"
#include "dbus/options.h"

#include <gio/gunixfdlist.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <format>
#include <print>
#include <random>
#include <utility>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <string_view>

namespace xdpn {

  namespace print {

    std::string defaultOutputPath(const std::string& title) {
      std::string name = title.empty() ? std::string("Document") : title;
      for (char& c : name) {
        if (c == '/') {
          c = '-';
        }
      }
      const std::filesystem::path home = g_get_home_dir();
      std::error_code error;
      const auto documents = home / "Documents";
      const auto folder = std::filesystem::is_directory(documents, error) ? documents : home;
      return (folder / (name + ".pdf")).string();
    }

  } // namespace print

  namespace {

    // Runs `work` in a GIO worker thread and `done` with its result back in the main loop.
    template <typename Result>
    void inThread(std::function<Result()> work, std::function<void(Result)> done) {
      struct Job {
        std::function<Result()> work;
        std::function<void(Result)> done;
        Result result{};
      };
      auto* job = new Job{std::move(work), std::move(done), {}};
      GTask* task = g_task_new(
          nullptr, nullptr,
          +[](GObject*, GAsyncResult* result, gpointer data) {
            auto* finished = static_cast<Job*>(data);
            (void)g_task_propagate_boolean(G_TASK(result), nullptr);
            finished->done(std::move(finished->result));
            delete finished;
          },
          job);
      g_task_set_task_data(task, job, nullptr);
      g_task_run_in_thread(task, +[](GTask* running, gpointer, gpointer data, GCancellable*) {
        auto* current = static_cast<Job*>(data);
        current->result = current->work();
        g_task_return_boolean(running, TRUE);
      });
      g_object_unref(task);
    }

    // Copies everything from `fd` to `path`; empty on success, else the error.
    std::string copyFd(int fd, const std::filesystem::path& path) {
      FILE* out = std::fopen(path.c_str(), "wb");
      if (out == nullptr) {
        return std::format("cannot write {}", path.string());
      }
      std::string error;
      char buffer[65536];
      if (lseek(fd, 0, SEEK_SET) < 0 && errno != ESPIPE) {
        error = "cannot rewind the document";
      }
      while (error.empty()) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
          continue;
        }
        if (count < 0) {
          error = "cannot read the document";
          break;
        }
        if (count == 0) {
          break;
        }
        if (std::fwrite(buffer, 1, static_cast<size_t>(count), out) != static_cast<size_t>(count)) {
          error = std::format("cannot write {}", path.string());
        }
      }
      if (std::fclose(out) != 0 && error.empty()) {
        error = std::format("cannot write {}", path.string());
      }
      return error;
    }

    GVariant* emptyReply(guint32 response) { return g_variant_new("(u@a{sv})", response, emptyResults()); }

  } // namespace

  Print::Print(GDBusConnection* connection) : m_calls(connection) {
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kPrint,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "PreparePrint") {
            preparePrint(parameters, invocation);
          } else if (method == "Print") {
            print(parameters, invocation);
          } else {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                  "Unknown method %s", method.c_str());
          }
        });
  }

  Print::~Print() { m_interface.reset(); }

  print::Media Print::mediaFor(const dialog::PrintJob& job) const {
    std::vector<print::Media> candidates = print::standardMedia();
    for (const auto& printer : m_printers) {
      if (printer.name == job.printer && !printer.media.empty()) {
        candidates = printer.media;
      }
    }
    for (const auto& media : candidates) {
      if (media.name == job.media) {
        return media;
      }
    }
    print::Media media = candidates.front();
    if (auto size = print::pwgSizeMm(job.media)) {
      media = {.name = job.media, .label = job.media, .widthMm = size->first, .heightMm = size->second,
               .topMm = 6.35, .bottomMm = 6.35, .leftMm = 6.35, .rightMm = 6.35};
    }
    return media;
  }

  guint32 Print::remember(const std::string& appId, const dialog::PrintJob& job) {
    static std::mt19937 random{std::random_device{}()};
    guint32 token = 0;
    do {
      token = static_cast<guint32>(random());
    } while (token == 0 || m_prepared.contains(token));
    m_prepared[token] = {.appId = appId, .job = job};
    return token;
  }

  void Print::ask(const std::string& handle, GDBusMethodInvocation* invocation, const std::string& title,
                  const dialog::PrintJob& preset, const std::string& acceptLabel, bool toFile, Chosen chosen) {
    inThread<std::vector<print::Printer>>(
        [] { return print::enumeratePrinters(); },
        [this, handle, invocation, title, preset, acceptLabel, toFile,
         chosen = std::move(chosen)](std::vector<print::Printer> printers) {
          m_printers = std::move(printers);
          dialog::Request request;
          request.kind = dialog::Kind::Print;
          request.title = title.empty() ? "Print" : std::format("Print “{}”", title);
          request.grantLabel = acceptLabel.empty() ? "Print" : acceptLabel;
          const print::Printer* fallback = nullptr;
          for (const auto& printer : m_printers) {
            dialog::PrintPrinter entry{.id = printer.name, .label = printer.label, .media = {},
                                       .duplex = printer.duplex, .color = printer.color, .toFile = false};
            for (const auto& media : printer.media.empty() ? print::standardMedia() : printer.media) {
              entry.media.push_back({.id = media.name, .label = media.label});
            }
            request.printers.push_back(std::move(entry));
            if (fallback == nullptr || printer.isDefault) {
              fallback = &printer;
            }
          }
          if (toFile) {
            dialog::PrintPrinter file{.id = print::kFilePrinter, .label = "Print to File", .media = {},
                                      .duplex = false, .color = true, .toFile = true};
            for (const auto& media : print::standardMedia()) {
              file.media.push_back({.id = media.name, .label = media.label});
            }
            request.printers.push_back(std::move(file));
          }
          request.printJob = preset;
          const bool known = std::ranges::any_of(request.printers, [&preset](const dialog::PrintPrinter& printer) {
            return printer.id == preset.printer;
          });
          if (!known) {
            request.printJob.printer = fallback != nullptr ? fallback->name : std::string(print::kFilePrinter);
            if (fallback != nullptr && !fallback->defaultMedia.empty() && preset.media.empty()) {
              request.printJob.media = fallback->defaultMedia;
            }
          }
          if (request.printJob.outputPath.empty()) {
            request.printJob.outputPath = print::defaultOutputPath(title);
          }
          if (request.printers.empty()) {
            g_dbus_method_invocation_return_value(invocation, emptyReply(dbus::Other));
            return;
          }
          m_calls.start(handle, invocation, request, chosen);
        });
  }

  void Print::preparePrint(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* parentWindow = nullptr;
    const char* title = nullptr;
    GVariant* settings = nullptr;
    GVariant* pageSetup = nullptr;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s&s@a{sv}@a{sv}@a{sv})", &handle, &appId, &parentWindow, &title, &settings,
                  &pageSetup, &options);
    const auto preset = print::jobFromSettings(settings, pageSetup);
    const auto acceptLabel = lookupString(options, "accept_label");
    bool toFile = true;
    const char** formats = nullptr;
    if (g_variant_lookup(options, "supported_output_file_formats", "^a&s", &formats)) {
      toFile = false;
      for (const char** format = formats; *format != nullptr; ++format) {
        toFile = toFile || std::string_view(*format) == "pdf";
      }
      g_free(formats);
    }
    g_variant_unref(settings);
    g_variant_unref(pageSetup);
    g_variant_unref(options);

    const std::string app = appId;
    ask(handle, invocation, title, preset, acceptLabel, toFile,
        [this, app](GDBusMethodInvocation* call, const dialog::Answer& answer) {
          if (answer.response != dbus::Success || answer.printJob.printer.empty()) {
            g_dbus_method_invocation_return_value(call, emptyReply(answer.response == dbus::Success ? dbus::Other
                                                                                                    : answer.response));
            return;
          }
          const auto& job = answer.printJob;
          const auto media = mediaFor(job);
          GVariantBuilder results;
          g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
          g_variant_builder_add(&results, "{sv}", "settings", print::settingsFromJob(job, media));
          g_variant_builder_add(&results, "{sv}", "page-setup", print::pageSetupFromJob(job, media));
          g_variant_builder_add(&results, "{sv}", "token", g_variant_new_uint32(remember(app, job)));
          g_dbus_method_invocation_return_value(call, g_variant_new("(ua{sv})", dbus::Success, &results));
        });
  }

  void Print::print(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* parentWindow = nullptr;
    const char* title = nullptr;
    gint32 fdIndex = -1;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s&sh@a{sv})", &handle, &appId, &parentWindow, &title, &fdIndex, &options);
    guint32 token = 0;
    g_variant_lookup(options, "token", "u", &token);
    g_variant_unref(options);

    GUnixFDList* fds = g_dbus_message_get_unix_fd_list(g_dbus_method_invocation_get_message(invocation));
    const int fd = fds != nullptr ? g_unix_fd_list_get(fds, fdIndex, nullptr) : -1;
    if (fd < 0) {
      g_dbus_method_invocation_return_value(invocation, emptyReply(dbus::Other));
      return;
    }

    if (auto it = m_prepared.find(token); it != m_prepared.end() && it->second.appId == appId) {
      const auto job = it->second.job;
      m_prepared.erase(it);
      printJob(invocation, title, fd, job);
      return;
    }
    const std::string documentTitle = title;
    ask(handle, invocation, title, {}, {}, true,
        [this, documentTitle, fd](GDBusMethodInvocation* call, const dialog::Answer& answer) {
          if (answer.response != dbus::Success || answer.printJob.printer.empty()) {
            close(fd);
            g_dbus_method_invocation_return_value(call, emptyReply(answer.response == dbus::Success ? dbus::Other
                                                                                                    : answer.response));
            return;
          }
          printJob(call, documentTitle, fd, answer.printJob);
        });
  }

  void Print::printJob(GDBusMethodInvocation* invocation, const std::string& title, int fd, const dialog::PrintJob& job) {
    const unsigned number = ++m_counter;
    inThread<std::string>(
        [title, fd, job, number]() -> std::string {
          std::string error;
          if (job.printer == print::kFilePrinter) {
            error = copyFd(fd, job.outputPath.empty() ? print::defaultOutputPath(title) : job.outputPath);
          } else {
            auto folder = std::filesystem::path(g_get_user_runtime_dir()) / "xdg-desktop-portal-noctalia";
            std::error_code fsError;
            std::filesystem::create_directories(folder, fsError);
            const auto pdf = folder / std::format("print-{}-{}.pdf", getpid(), number);
            error = copyFd(fd, pdf);
            if (error.empty()) {
              error = print::submit(job.printer, title.empty() ? std::string("Document") : title, pdf, job);
            }
            std::filesystem::remove(pdf, fsError);
          }
          close(fd);
          return error;
        },
        [invocation](std::string error) {
          if (!error.empty()) {
            std::println(stderr, "print: {}", error);
          }
          g_dbus_method_invocation_return_value(invocation, emptyReply(error.empty() ? dbus::Success : dbus::Other));
        });
  }

} // namespace xdpn
