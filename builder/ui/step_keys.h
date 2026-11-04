#pragma once
// A chave do catalogo de cada etapa do Builder (as janelas do Windows e o instalador do N9.1, nos dois sistemas).
#include "builder_core.h"

namespace bs::builder::ui {

inline const char* step_key(Step s) {
    switch (s) {
    case Step::Validate: return "step.validate";
    case Step::Toolchain: return "step.toolchain";
    case Step::Extract: return "step.extract";
    case Step::Recompile: return "step.recompile";
    case Step::Configure: return "step.configure";
    case Step::Compile: return "step.compile";
    case Step::Package: return "step.package";
    case Step::Verify: return "step.verify";
    case Step::Export: return "step.export";
    case Step::Install: return "step.install";
    case Step::Done: return "step.done";
    }
    return "step.validate";
}

} // namespace bs::builder::ui
