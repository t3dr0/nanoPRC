#include <memory>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>

extern "C" {
#include "prc_api.h"
}

namespace py = pybind11;

namespace {

class Context {
public:
    Context() : ctx_(prc_api_new_context(nullptr)) {
        if (!ctx_) {
            throw std::runtime_error("Failed to create prc_context");
        }
    }

    ~Context() {
        if (ctx_) {
            (void)prc_api_release_context(ctx_);
            ctx_ = nullptr;
        }
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Context(Context&& other) noexcept : ctx_(other.ctx_) {
        other.ctx_ = nullptr;
    }

    Context& operator=(Context&& other) noexcept {
        if (this != &other) {
            if (ctx_) {
                (void)prc_api_release_context(ctx_);
            }
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }

    prc_context* raw() const {
        return ctx_;
    }

    void print_error_stack() const {
        prc_api_print_error_stack(ctx_);
    }

private:
    prc_context* ctx_;
};

class Document {
public:
    Document(std::shared_ptr<Context> owner, prc_api_data data)
        : owner_(std::move(owner)), data_(data) {}

    ~Document() {
        if (data_ && owner_ && owner_->raw()) {
            prc_api_release_data(owner_->raw(), data_, nullptr, 0, nullptr, 0, nullptr);
            data_ = nullptr;
        }
    }

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Document(Document&& other) noexcept
        : owner_(std::move(other.owner_)), data_(other.data_) {
        other.data_ = nullptr;
    }

    Document& operator=(Document&& other) noexcept {
        if (this != &other) {
            if (data_ && owner_ && owner_->raw()) {
                prc_api_release_data(owner_->raw(), data_, nullptr, 0, nullptr, 0, nullptr);
            }
            owner_ = std::move(other.owner_);
            data_ = other.data_;
            other.data_ = nullptr;
        }
        return *this;
    }

    bool is_open() const {
        return data_ != nullptr;
    }

private:
    std::shared_ptr<Context> owner_;
    prc_api_data data_;
};

Document context_open(const std::shared_ptr<Context>& ctx, const std::string& infile) {
    if (!ctx || !ctx->raw()) {
        throw std::runtime_error("Context is not initialized");
    }

    prc_api_data data = prc_api_open_contents(ctx->raw(), infile.c_str());
    if (!data) {
        throw std::runtime_error("prc_api_open_contents failed");
    }

    return Document(ctx, data);
}

} // namespace

PYBIND11_MODULE(_core, m) {
    m.doc() = "Initial nanoPRC Python bindings";

    m.attr("PRC_API_ERROR_MEMORY") = py::int_(PRC_API_ERROR_MEMORY);
    m.attr("PRC_API_ERROR_PARAMETER") = py::int_(PRC_API_ERROR_PARAMETER);
    m.attr("PRC_API_ERROR_PARSER") = py::int_(PRC_API_ERROR_PARSER);
    m.attr("PRC_API_ERROR_UNSUPPORTED") = py::int_(PRC_API_ERROR_UNSUPPORTED);

    py::class_<Context, std::shared_ptr<Context>>(m, "Context")
        .def(py::init<>())
        .def("open", &context_open, py::arg("infile"), "Open PRC/PDF contents")
        .def("print_error_stack", &Context::print_error_stack,
             "Print current nanoPRC error stack to stdout");

    py::class_<Document>(m, "Document")
        .def_property_readonly("is_open", &Document::is_open,
                               "Whether the underlying PRC data handle is valid");
}
