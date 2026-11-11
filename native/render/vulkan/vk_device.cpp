#include "render/vulkan/vk_device.h"

#include "render/frame_prep.h"
#include "render/water.h"
#include "render/material_catalog.h"
#include "render/vulkan/spirv/pvr_frag.h"
#include "render/vulkan/spirv/pvr_frag_autosort.h"
#include "render/vulkan/spirv/pvr_frag_autosort_fc.h"
#include "render/vulkan/spirv/pvr_frag_autosort_fcd.h"
#include "render/vulkan/spirv/pvr_frag_fc.h"
#include "render/vulkan/spirv/pvr_frag_fcd.h"
#include "render/vulkan/spirv/pvr_frag_water.h"  // N9.7: as variantes -DWATER
#include "render/vulkan/spirv/pvr_frag_autosort_water.h"
#include "render/vulkan/spirv/pvr_frag_fc_water.h"
#include "render/vulkan/spirv/pvr_frag_autosort_fc_water.h"
#include "render/vulkan/spirv/pvr_frag_fcd_water.h"
#include "render/vulkan/spirv/pvr_frag_autosort_fcd_water.h"
#include "render/soft/soft_raster.h"
#include "render/vulkan/spirv/pvr_vert.h"
#include "render/vulkan/spirv/ao_blur_comp.h"
#include "render/vulkan/spirv/ao_blur_surf_comp.h"
#include "render/vulkan/spirv/ao_comp.h"
#include "render/vulkan/spirv/ao_half_comp.h"
#include "render/vulkan/spirv/ao_up_comp.h"
#include "render/vulkan/spirv/ao_up_surf_comp.h"
#include "render/vulkan/spirv/lights_comp.h"
#include "render/vulkan/spirv/soft_shadows_comp.h"
#include "render/vulkan/spirv/volumes_mask_comp.h"
#include "render/vulkan/spirv/volumes_mask_comp_fc.h"
#include "render/vulkan/spirv/volumes_mask_comp_fcd.h"
#include "render/vulkan/spirv/volumes_comp.h"
#include "render/vulkan/spirv/volumes_comp_fc.h"
#include "render/vulkan/spirv/volumes_comp_fcd.h"
#include "render/vulkan/spirv/resolve_comp.h"
#include "render/vulkan/spirv/resolve_comp_fc.h"
#include "render/vulkan/spirv/resolve_comp_fcd.h"
#include "render/vulkan/spirv/downsample_comp.h"

#include "render/vulkan/vk_internal.h"
#include "platform/stall_trace.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  undef DrawState  // WinUser.h: DrawStateW (colide com render::DrawState)
#else
#  include <dlfcn.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <tuple>
#include <map>
#include <limits>
#include <unordered_map>

namespace bs::native::render {
namespace {


// Constantes de envio de um lote: o layout de `Batch` em pvr.vert/pvr.frag
struct Push {
    float    size[2];
    float    depthScale;
    uint32_t flags;
    int32_t  clip[4];
    uint32_t texSize[2];
    uint32_t ptRef, blend, clampMin, clampMax;
    float    depthBias;  // subtraido de 1/w antes da escala (so com 1/w negativo no quadro)
    float    invScale;   // 1 / escala da resolucao interna (N7c)
    // N9.7: so a variante -DWATER le (qualidade, fase, horizonte e fundo); o fiel recebe e le so os 64 bytes de antes
    float    water[4];
};
static_assert(sizeof(Push) == 80 && offsetof(Push, water) == 64);

constexpr uint32_t F_TEXTURED = 1, F_GOURAUD = 2, F_OFFSET = 4, F_USE_ALPHA = 8, F_IGNORE_TEX_ALPHA = 16, F_CLAMP = 32,
                   F_CLIP_OUTSIDE = 64, F_CLAMP_U = 4096, F_CLAMP_V = 8192, F_FLIP_U = 16384, F_FLIP_V = 32768,
                   F_TEXEL_HALF = 65536, F_ATTR_PLUS = 1u << 19, F_ATTR_MINUS = 1u << 20, F_EXACT = 1u << 21,
                   F_SAMPLE_CORNER = 1u << 22, F_ATTR_CORNER = 1u << 23, F_RAW_DEPTH = 1u << 24,
                   F_FOG = 1u << 25,     // N8.7: fog por tabela
                   F_SHADOW = 1u << 26,  // N8.7: PCW bit 7 (o passe das opacas grava o bit de sombra do pixel)
                   F_SCENE  = 1u << 27,  // N9.6 fase 1: desenho da cena 3D (bit 1 do anexo de sombra; o AO so age nele)
                   F_AO_SURF = 1u << 28;  // N9.6 fase 2b: translucida do catalogo: o AO do opaco no plano dela
constexpr uint32_t kFragBytes = 32;  // struct Frag (std430)
constexpr uint32_t kFrameDataBytes = 64 * 4 + 2 * 4;  // N8.7: FrameData de pvr.frag (fog)
constexpr uint32_t kHeader    = 32;  // cabecalho das listas por pixel (Heads: 8 palavras)

struct Tex {
    TextureDesc     desc;
    Image           image;
    VkDescriptorSet set = VK_NULL_HANDLE;
    bool            ready = false;  // ja recebeu dados (layout de leitura)
    uint32_t        material = 0;   // N9.6 fase 2b: os bits do catalogo de materiais (set_texture_material)
    uint32_t        light = 0xFFFFFF;  // N9.6 fase 3: a cor da luz de uma fonte (set_texture_light)
};

class VkDev final : public VulkanDevice, public VkBase, public SlotSource {
public:
    ~VkDev() override { destroy(); }

    bool init(std::string* error, const std::vector<std::string>& instanceExtensions) {
        if (!load(error)) return false;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Blue Stinger Native PC", app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        // N7c: as extensoes da superficie da janela (apresentacao direta); sem elas, so offscreen
        std::vector<const char*> iext;
        for (const std::string& e : instanceExtensions) iext.push_back(e.c_str());
        ici.enabledExtensionCount = uint32_t(iext.size()), ici.ppEnabledExtensionNames = iext.data();
        surfaceExt_ = !iext.empty();
        if (!ok(f_.vkCreateInstance(&ici, nullptr, &inst_), "vkCreateInstance", error)) return false;
        ++g_live;
#define LOADI(n) f_.n = reinterpret_cast<PFN_##n>(f_.gipa(inst_, #n));
        VK_IFUNCS(LOADI)
        VK_SFUNCS(LOADI)
#undef LOADI
        if (!pick(error)) return false;
        telemetry::set_device(st_.device, tsPeriod_ > 0);  // N9.5 fase 5C (o nome da GPU, sem dados pessoais)
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        const float prios[2] = {1.0f, 1.0f};
        qi.queueFamilyIndex = qfam_, qi.queueCount = std::min(queueCount_, 2u), qi.pQueuePriorities = prios;
        VkPhysicalDeviceFeatures feat{};
        feat.depthClamp = VK_TRUE, feat.fragmentStoresAndAtomics = VK_TRUE;
        // acessos fora dos limites dos buffers devolvem valores definidos em vez de falha de pagina (obrigatorio no
        // Vulkan 1.0; perda do dispositivo na GPU AMD integrada, 2026-10-22)
        feat.robustBufferAccess = VK_TRUE;
        // Garantias do modo fiel (opcionais; sem elas o backend roda, mas a igualdade bit a bit com o software deixa
        // de ser garantida e o resumo diz isso):
        //   VK_KHR_shader_float_controls com RTE e preservacao de subnormais em float32: o ambiente FP do software;
        //   VK_EXT_depth_range_unrestricted: a profundidade guarda o proprio 1/w (inclusive negativo), sem transformacao.
        uint32_t ne = 0;
        f_.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &ne, nullptr);
        std::vector<VkExtensionProperties> exts(ne);
        f_.vkEnumerateDeviceExtensionProperties(phys_, nullptr, &ne, exts.data());
        auto has = [&](const char* n) {
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, n) == 0) return true;
            return false;
        };
        std::vector<const char*> ext;
        if (has(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME)) {
            VkPhysicalDeviceFloatControlsProperties fc{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
            VkPhysicalDeviceProperties2             p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            p2.pNext = &fc;
            f_.vkGetPhysicalDeviceProperties2(phys_, &p2);
            if (fc.shaderRoundingModeRTEFloat32) {
                floatControls_  = true, ext.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
                denormPreserve_ = fc.shaderDenormPreserveFloat32;
            }
        }
        // testes (N8.7): BS_VK_SEM_PROFUNDIDADE_CRUA=1 exercita o caminho dos dispositivos sem a extensao
        if (has(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME) && !std::getenv("BS_VK_SEM_PROFUNDIDADE_CRUA"))
            rawDepth_ = true, ext.push_back(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
        if (surfaceExt_ && has(VK_KHR_SWAPCHAIN_EXTENSION_NAME))  // apresentacao direta (N7c)
            swapchainExt_ = true, ext.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        // N9.5 fase 5C: o orcamento da memoria so com a telemetria ligada (sem ela, o dispositivo e o de sempre)
        if (telemetry::enabled() && has(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
            budgetExt_ = true, ext.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1, dci.pQueueCreateInfos = &qi, dci.pEnabledFeatures = &feat;
        dci.enabledExtensionCount = uint32_t(ext.size()), dci.ppEnabledExtensionNames = ext.data();
        st_.float_controls = floatControls_, st_.denorm_preserve = denormPreserve_, st_.raw_depth = rawDepth_;
        if (!ok(f_.vkCreateDevice(phys_, &dci, nullptr, &dev_), "vkCreateDevice", error)) return false;
        ++g_live;
#define LOADD(n) f_.n = reinterpret_cast<PFN_##n>(f_.vkGetDeviceProcAddr(dev_, #n));
        VK_DFUNCS(LOADD)
        VK_PFUNCS(LOADD)
#undef LOADD
        f_.vkGetDeviceQueue(dev_, qfam_, 0, &queue_);
        presentQueue_ = queue_;
        if (qi.queueCount > 1) f_.vkGetDeviceQueue(dev_, qfam_, 1, &presentQueue_);
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, pci.queueFamilyIndex = qfam_;
        if (!ok(f_.vkCreateCommandPool(dev_, &pci, nullptr, &pool_), "vkCreateCommandPool", error)) return false;
        ++g_live;
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool_, cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, cai.commandBufferCount = 1;
        if (!ok(f_.vkAllocateCommandBuffers(dev_, &cai, &cmd_), "vkAllocateCommandBuffers", error)) return false;
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!ok(f_.vkCreateFence(dev_, &fci, nullptr, &fence_), "vkCreateFence", error)) return false;
        ++g_live;
        return layouts(error);
    }

    // ---- IRenderDevice ----
    Result<TextureHandle> create_texture(const TextureDesc& desc, std::span<const std::byte> data) override {
        if (desc.format != PixelFormat::ARGB8888 || !desc.width || !desc.height) return Error::Unsupported;
        Tex       t;
        MemScope  mc(telemetry::Mem::Textures);  // N9.5 fase 5C
        t.desc = desc, t.desc.mip_levels = 1;  // o software amostra so o maior nivel (fase de fidelidade)
        t.image = makeImage(desc.width, desc.height, VK_FORMAT_B8G8R8A8_UNORM,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!t.image.img) return Error::Io;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = texPool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &texLayout_;
        if (f_.vkAllocateDescriptorSets(dev_, &ai, &t.set) != VK_SUCCESS) {
            freeImage(t.image);
            return Error::Io;
        }
        VkDescriptorImageInfo ii{sampler_, t.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet  w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = t.set, w.descriptorCount = 1, w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w.pImageInfo = &ii;
        f_.vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
        const uint32_t id = ++nextTex_;
        texs_[id]         = t;
        TextureHandle h;
        h.value = id;
        if (auto r = update_texture(h, data); !r) return r.error();
        return h;
    }

    Result<void> update_texture(TextureHandle h, std::span<const std::byte> data) override {
        auto it = texs_.find(h.value);
        if (it == texs_.end()) return Error::InvalidHandle;
        const size_t bytes = size_t(it->second.desc.width) * it->second.desc.height * 4;
        if (data.size() < bytes) return Error::OutOfRange;
        // a copia vai no proximo quadro (antes do passe de render); o dado fica na area de envio
        const size_t off = uploadBytes_.size();
        uploadBytes_.insert(uploadBytes_.end(), data.begin(), data.begin() + bytes);
        uploads_.push_back({h.value, off});
        ++st_.uploads;
        return {};
    }

    void set_palette(std::span<const uint32_t>) override {}  // as texturas chegam ja convertidas (ARGB8888)

    void destroy_texture(TextureHandle h) override {
        auto it = texs_.find(h.value);
        if (it == texs_.end()) return;
        // sem quadro em voo (cada submit espera o seu): pode destruir ja
        f_.vkFreeDescriptorSets(dev_, texPool_, 1, &it->second.set);
        freeImage(it->second.image);
        uploads_.erase(std::remove_if(uploads_.begin(), uploads_.end(), [&](const Upload& u) { return u.tex == h.value; }),
                       uploads_.end());
        texs_.erase(it);
    }

    Result<void> submit(const CommandList& cl) override {
        // diagnostico BS_VK_LENTOS=<ms>: cada render mais lento que isso, com o que ele fez (o tempo de GPU, se foi refeito
        // por falta de espaco nas listas por pixel, pipelines criados, textura enfileirada para este render) - os picos dos
        // campos (N2, 2026-10-23). Desligado, so chama submitFrame.
        static const double lentos = [] {
            const char* v = std::getenv("BS_VK_LENTOS");
            return v ? std::atof(v) : 0.0;
        }();
        if (lentos <= 0) return submitFrame(cl);
        const auto     t0   = std::chrono::steady_clock::now();
        const uint64_t ret0 = st_.fragment_retries, pip0 = pipelinesCreated_;
        const double   gpu0 = st_.gpu_ms_total;
        const size_t   up0  = uploadBytes_.size();
        auto           r    = submitFrame(cl);
        const double   ms   = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms >= lentos)
            std::fprintf(stderr, "[render-vk] render lento %llu: %.1f ms (GPU %.1f ms; refeito %llu; pipelines novos %llu; textura %zu bytes; %ux%u; camadas %u, fragmentos %u)\n",
                         static_cast<unsigned long long>(st_.frames), ms, st_.gpu_ms_total - gpu0,
                         static_cast<unsigned long long>(st_.fragment_retries - ret0),
                         static_cast<unsigned long long>(pipelinesCreated_ - pip0), up0, w_, h_, lastLayers_, lastFrags_);
        return r;
    }

    Result<void> submitFrame(const CommandList& cl) {
        // N9.7: a agua aprimorada neste quadro: a qualidade ligada, o render do framebuffer e algum desenho translucido de
        // uma textura do catalogo com kMaterialWater; o horizonte e o fundo vem dos y desses desenhos (water_extent)
        waterQuality_ = WaterQuality(waterQ_.load(std::memory_order_relaxed));
        waterWanted_  = false;
        if (waterQuality_ != WaterQuality::Original && cl.frame.height == 480 && cl.frame.width >= 640 && !waterFailed_)
            waterWanted_ = water_extent(cl, [&](const Draw& d) { return waterDraw(d.state); }, waterHorizon_, waterBottom_);
        waterTime_ = water_phase(waterRender_.load(std::memory_order_relaxed));
        MemScope memScope(telemetry::Mem::FrameBuffers);  // N9.5 fase 5C: vertices, triangulos, envios (o alvo tem o seu)
        prepare_frame(cl, prep_, true, scale_);  // modo fiel: retangulos envolventes, a cobertura e a do shader
        if (!target(scaled_size(cl.frame.width, scale_), scaled_size(cl.frame.height, scale_), cl.frame.width, cl.frame.height))
            return fail("alvo do quadro (imagens/passe)");
        // N7c: com a escala, a imagem maior vai para um slot livre (nenhuma apresentacao usando); sem slot livre, o quadro
        // sai so na resolucao do jogo
        writeSlot_ = -1;
        if (scale_ != 1.f || alwaysSlot_) {
            std::lock_guard<std::mutex> lock(slotMutex_);
            for (int i = 0; i < kSlots; ++i)
                if (!slots_[i].pins && (writeSlot_ < 0 || slots_[i].seq < slots_[writeSlot_].seq)) writeSlot_ = i;
            if (writeSlot_ >= 0) {
                Slot& sl = slots_[writeSlot_];
                sl.seq   = 0;  // em escrita: nao pode ser fixado pela apresentacao
                // com duas filas, um semaforo novo a cada escrita: o uso anterior (sinal e, se houve, a espera da
                // apresentacao) ja terminou, porque o slot so e reaproveitado sem fixacao (revisao Codex 2026-10-22).
                // Com uma fila so, nenhum semaforo: a ordem de submissao da a dependencia.
                if (presentQueue_ != queue_ && !remakeReady(writeSlot_)) writeSlot_ = -1;
                if (sl.w != w_ || sl.h != h_) {
                    MemScope mc(telemetry::Mem::Slots);  // N9.5 fase 5C
                    freeImage(sl.image);
                    sl.image = makeImage(w_, h_, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                         VK_IMAGE_ASPECT_COLOR_BIT);
                    sl.w = sl.image.img ? w_ : 0, sl.h = sl.image.img ? h_ : 0, sl.shaderRead = false, ++sl.version;
                    if (!sl.image.img) writeSlot_ = -1;
                }
            }
            if (writeSlot_ < 0) ++st_.slots_skipped;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (auto r = record(cl); !r) return r;
            const uint32_t* head = static_cast<const uint32_t*>(headerRead_.map);
            const uint32_t  used = head[0], overflow = head[2];
            st_.max_layers = std::max<uint64_t>(st_.max_layers, head[4]);
            lastLayers_ = head[4], lastFrags_ = used;
            st_.fragments += std::min(used, fragCapacity_);
            if (!overflow) break;
            // a lista do pixel nao coube: cresce e refaz o MESMO quadro (sem perder fragmentos); o semaforo do slot ja foi
            // sinalizado pela tentativa anterior (concluida: cerca) e e refeito para a nova sinalizar
            ++st_.fragment_retries;
            if (writeSlot_ >= 0 && presentQueue_ != queue_ && !remakeReady(writeSlot_)) writeSlot_ = -1;
            if (!fragments(used + used / 2 + 1024)) return fail("listas por pixel (memoria)");
        }
        // N9.7 (revisao Codex 20261111-121354): a agua conta so o render concluido (a tentativa que transbordou e refeita)
        st_.water_frames += waterFramesRec_, st_.water_draws += waterDrawsRec_;
        st_.frames++, st_.triangles += prep_.triangles, st_.culled += prep_.culled;
        st_.nonfinite_depth += prep_.nonfinite_depth, st_.batches += prep_.batches.size();
        st_.negative_depth_frames += prep_.depth_bias != 0;  // so importa sem profundidade crua
        uploads_.clear(), uploadBytes_.clear();
        imageW_ = gameW_, imageH_ = gameH_;  // a leitura de volta e sempre na resolucao do jogo
        {
            std::lock_guard<std::mutex> lock(slotMutex_);
            lastSlot_ = 0;
            if (writeSlot_ >= 0) slots_[writeSlot_].seq = lastSlot_ = ++slotSeq_, slots_[writeSlot_].readyPending = presentQueue_ != queue_, ++st_.slots_written;
        }
        return {};
    }

    void  set_scale(float s) override { scale_ = s > 0 ? s : 1.f; }
    void  set_always_slot(bool on) override { alwaysSlot_ = on; }
    void  set_ambient_occlusion(bool on) override { aoOn_.store(on, std::memory_order_relaxed); }
    void  set_ambient_occlusion_quality(AoQuality q) override { aoQuality_.store(uint8_t(q), std::memory_order_relaxed); }
    void  set_lighting(bool on) override { lightOn_.store(on, std::memory_order_relaxed); }  // N9.6 fase 3
    void  set_shadows(bool on) override { shadowsOn_.store(on, std::memory_order_relaxed); }  // N9.6 fase 4
    void  set_water(WaterQuality q, uint64_t render) override {  // N9.7
        waterQ_.store(uint8_t(q), std::memory_order_relaxed), waterRender_.store(render, std::memory_order_relaxed);
    }
    // N9.7: um desenho de agua: translucido, de uma textura do catalogo com kMaterialWater (o handle do dispositivo; nada
    // no handle muda)
    bool waterDraw(const DrawState& s) const {
        if (s.list != ListType::Translucent || !s.texture) return false;
        const auto it = texs_.find(s.texture.value);
        return it != texs_.end() && (it->second.material & kMaterialWater);
    }
    void  set_texture_light(TextureHandle t, uint32_t rgb) override {  // N9.6 fase 3
        if (auto it = texs_.find(t.value); it != texs_.end()) it->second.light = rgb;
    }
    void  set_texture_material(TextureHandle t, uint32_t flags) override {  // N9.6 fase 2b
        if (auto it = texs_.find(t.value); it != texs_.end()) it->second.material = flags;
    }
    float scale() const override { return scale_; }

    bool remakeReady(int i) {
        auto& sl = slots_[i];
        if (sl.ready) f_.vkDestroySemaphore(dev_, sl.ready, nullptr), --g_live, sl.ready = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (f_.vkCreateSemaphore(dev_, &sci, nullptr, &sl.ready) != VK_SUCCESS) return sl.ready = VK_NULL_HANDLE, false;
        ++g_live, sl.readyPending = false;
        return true;
    }

    uint64_t last_slot(uint32_t& width, uint32_t& height) const override {
        width = w_, height = h_;
        return lastSlot_;
    }

    // capturas e testes (thread do jogo, a mesma do submit: usa o buffer de comandos dele)
    bool read_slot(uint64_t seq, uint32_t y0, uint32_t rows, std::vector<uint32_t>& argb, uint32_t& width) override {
        VkImage  img = VK_NULL_HANDLE;
        VkImageView view;
        uint32_t w = 0, h = 0;
        int      idx = -1;
        VkSemaphore unused = VK_NULL_HANDLE;
        if (!seq || !pinShared(seq, img, view, w, h, idx, nullptr, unused)) return false;
        rows = std::min(rows, h > y0 ? h - y0 : 0u);
        bool ok = rows > 0;
        Buffer buf;
        if (ok) buf = makeBuffer(VkDeviceSize(w) * rows * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true), ok = buf.map != nullptr;
        if (ok) {
            f_.vkResetCommandBuffer(cmd_, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            f_.vkBeginCommandBuffer(cmd_, &bi);
            // o slot fica em GENERAL: a copia so le (a apresentacao pode amostrar ao mesmo tempo); a escrita dele foi na
            // mesma fila (a do renderizador), antes
            barrier(f_, cmd_, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy c{};
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, c.imageOffset = {0, int32_t(y0), 0}, c.imageExtent = {w, rows, 1};
            f_.vkCmdCopyImageToBuffer(cmd_, img, VK_IMAGE_LAYOUT_GENERAL, buf.buf, 1, &c);
            VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
            f_.vkEndCommandBuffer(cmd_);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1, si.pCommandBuffers = &cmd_;
            f_.vkResetFences(dev_, 1, &fence_);
            const uint64_t trb = bs::native::diag::start();  // BS_TRAVAS (N8.8, H9): trava + envio + cerca
            uint64_t       trl = 0;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                trl = bs::native::diag::mark(trb);  // so o instante dentro da trava
                ok = f_.vkQueueSubmit(queue_, 1, &si, fence_) == VK_SUCCESS;
            }
            bs::native::diag::between(bs::native::diag::Stall::GameQueueLock, trb, trl);
            ok = ok && f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
            bs::native::diag::finish(bs::native::diag::Stall::GameReadback, trb);
        }
        if (ok) {
            const uint8_t* p = static_cast<const uint8_t*>(buf.map);
            argb.resize(size_t(w) * rows);
            for (size_t i = 0; i < argb.size(); ++i, p += 4)
                argb[i] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
            width = w, st_.readback_bytes += argb.size() * 4;
        }
        freeBuffer(buf);
        unpin(idx);
        return ok;
    }

    // ---- apresentacao direta (vk_present.cpp) ----
    uint64_t instance_handle() const override { return reinterpret_cast<uint64_t>(inst_); }
    bool     present_attach(uint64_t surface, std::string* error) override {
        if (!swapchainExt_) {
            if (error) *error = surfaceExt_ ? "o dispositivo nao tem VK_KHR_swapchain" : "instancia criada sem as extensoes da superficie";
            return false;
        }
        if (!presenter_) presenter_ = make_presenter(*this, *this);
        return presenter_->attach(reinterpret_cast<VkSurfaceKHR>(surface), error);
    }
    void present_detach() override {
        if (presenter_) presenter_->detach();
    }
    Result<void> present(const PresentRequest& r) override { return presenter_ ? presenter_->present(r) : Result<void>(Error::NotFound); }
    const PresentStats& present_stats() const override {
        static const PresentStats none;
        return presenter_ ? presenter_->stats() : none;
    }

    // ---- pos-processamento (vk_post.cpp) ----
    PostChain* post_chain() override {
        std::lock_guard<std::mutex> lock(postMutex_);
        if (!post_ && !postFailed_) {
            std::string err;
            post_ = make_post_chain(*this, &err);
            if (!post_) postFailed_ = true, std::fprintf(stderr, "[pos] cadeia indisponivel: %s\n", err.c_str());
        }
        return post_.get();
    }
    int post_load(std::span<const uint32_t> spirv, std::string* error) override {
        PostChain* c = post_chain();
        if (!c) {
            if (error) *error = "cadeia de pos-processamento indisponivel";
            return -1;
        }
        return c->load(spirv, error);
    }
    int post_builtin_color() override {
        PostChain* c = post_chain();
        return c ? c->builtin_color() : -1;
    }
    int post_builtin(PostBuiltin which) override {
        PostChain* c = post_chain();
        return c ? c->builtin(which) : -1;
    }
    std::vector<PostTiming> post_timings() override {
        PostChain* c = post_chain();
        return c ? c->timings() : std::vector<PostTiming>{};
    }
    // testes: a cadeia fora da janela, com o buffer de comandos do renderizador (a mesma thread dele)
    Result<void> post_run(std::span<const uint32_t> argb, uint32_t w, uint32_t h, std::span<const PostPass> passes,
                          std::vector<uint32_t>& out) override {
        PostChain* c = post_chain();
        if (!c || argb.size() < size_t(w) * h) return Error::Unsupported;
        // sem passes, a propria imagem e copiada de volta (TRANSFER_SRC; Validation Layers, N8.7)
        Image  src = makeImage(w, h, VK_FORMAT_B8G8R8A8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT);
        Buffer up = makeBuffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        Buffer rd = makeBuffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        bool   ok = src.img && up.map && rd.map;
        if (ok) {
            std::memcpy(up.map, argb.data(), size_t(w) * h * 4);
            f_.vkResetCommandBuffer(cmd_, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            f_.vkBeginCommandBuffer(cmd_, &bi);
            barrier(f_, cmd_, src.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy c1{};
            c1.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, c1.imageExtent = {w, h, 1};
            f_.vkCmdCopyBufferToImage(cmd_, up.buf, src.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c1);
            barrier(f_, cmd_, src.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
            const VkImageView v   = c->record(cmd_, src.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, w, h, passes);
            const VkImage     res = v ? c->image_of(v) : src.img;
            barrier(f_, cmd_, res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
            f_.vkCmdCopyImageToBuffer(cmd_, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rd.buf, 1, &c1);
            VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
            f_.vkEndCommandBuffer(cmd_);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1, si.pCommandBuffers = &cmd_;
            f_.vkResetFences(dev_, 1, &fence_);
            const uint64_t trb = bs::native::diag::start();  // BS_TRAVAS (N8.8, H9): trava + envio + cerca
            uint64_t       trl = 0;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                trl = bs::native::diag::mark(trb);  // so o instante dentro da trava
                ok = f_.vkQueueSubmit(queue_, 1, &si, fence_) == VK_SUCCESS;
            }
            bs::native::diag::between(bs::native::diag::Stall::GameQueueLock, trb, trl);
            ok = ok && f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
            bs::native::diag::finish(bs::native::diag::Stall::GameReadback, trb);
            if (ok) {
                out.resize(size_t(w) * h);
                const uint8_t* p = static_cast<const uint8_t*>(rd.map);
                if (!v) std::memcpy(out.data(), p, out.size() * 4);  // a propria imagem (B8G8R8A8 = ARGB)
                else
                    for (size_t i = 0; i < out.size(); ++i, p += 4)  // RGBA -> ARGB
                        out[i] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
            }
        }
        freeImage(src), freeBuffer(up), freeBuffer(rd);
        return ok ? Result<void>() : Result<void>(Error::Io);
    }

    // SlotSource: a apresentacao fixa o slot enquanto o desenha (o renderizador nao o reaproveita)
    bool pin(uint64_t seq, VkImage& img, VkImageView& view, uint32_t& w, uint32_t& h, int& idx, VkSemaphore& wait,
             uint64_t& version) override {
        bool consume = true;
        return pinShared(seq, img, view, w, h, idx, &consume, wait, &version);
    }
    // consume != nullptr: a apresentacao (outra fila) recebe o semaforo da escrita na primeira fixacao
    bool pinShared(uint64_t seq, VkImage& img, VkImageView& view, uint32_t& w, uint32_t& h, int& idx, bool* consume,
                   VkSemaphore& wait, uint64_t* version = nullptr) {
        std::lock_guard<std::mutex> lock(slotMutex_);
        wait = VK_NULL_HANDLE;
        for (int i = 0; i < kSlots; ++i)
            if (seq && slots_[i].seq == seq && slots_[i].shaderRead) {
                ++slots_[i].pins;
                img = slots_[i].image.img, view = slots_[i].image.view, w = slots_[i].w, h = slots_[i].h, idx = i;
                if (version) *version = slots_[i].version;
                if (consume && slots_[i].readyPending && presentQueue_ != queue_) wait = slots_[i].ready;
                if (consume) slots_[i].readyPending = false;  // a mesma fila: a ordem de submissao basta
                return true;
            }
        return false;
    }
    void unpin(int idx) override {
        std::lock_guard<std::mutex> lock(slotMutex_);
        if (idx >= 0 && idx < kSlots && slots_[idx].pins > 0) --slots_[idx].pins;
    }

    Result<void> present_image(std::span<const uint32_t> argb, uint32_t width, uint32_t height) override {
        if (argb.size() < size_t(width) * height) return Error::OutOfRange;
        frameImage_.assign(argb.begin(), argb.begin() + size_t(width) * height);
        imageW_ = width, imageH_ = height, fromImage_ = true;
        return {};
    }
    Result<void> present() override { return {}; }  // offscreen: o jogo apresenta
    void         resize(uint32_t, uint32_t) override {}

    Result<void> read_frame(std::vector<uint32_t>& argb, uint32_t& width, uint32_t& height) override {
        width = imageW_, height = imageH_;
        if (fromImage_) {
            argb = frameImage_;
            fromImage_ = false;
            return {};
        }
        if (!readback_.map) return Error::NotFound;
        const uint8_t* p = static_cast<const uint8_t*>(readback_.map);
        argb.resize(size_t(width) * height);
        for (size_t i = 0; i < argb.size(); ++i, p += 4)  // RGBA -> ARGB
            argb[i] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        return {};
    }

    const VulkanStats& stats() const override { return st_; }

private:
    VkCommandPool    pool_ = VK_NULL_HANDLE;
    VkCommandBuffer  cmd_ = VK_NULL_HANDLE;
    VkFence          fence_ = VK_NULL_HANDLE;
    uint64_t         pipelinesCreated_ = 0;  // diagnostico BS_VK_LENTOS
    uint32_t         lastLayers_ = 0, lastFrags_ = 0;  // BS_VK_LENTOS: maior lista e fragmentos do ultimo render
    VulkanStats      st_;
    bool             floatControls_ = false, denormPreserve_ = false, rawDepth_ = false;
    float            scale_ = 1.f;  // resolucao interna (N7c)
    bool             alwaysSlot_ = false;  // N9.5 fase 1: slot tambem na escala 1 (o quadro largo)
    bool             surfaceExt_ = false, swapchainExt_ = false;  // apresentacao direta possivel (N7c)
    bool             budgetExt_ = false;                          // N9.5 fase 5C: VK_EXT_memory_budget (so com a telemetria)
    VkQueryPool      tsPool_ = VK_NULL_HANDLE;                    // N9.5 fase 5C: timestamps por etapa (so com a telemetria)
    std::unique_ptr<PresenterImpl> presenter_;
    std::unique_ptr<PostChain>     post_;
    std::mutex                     postMutex_;
    bool                           postFailed_ = false;

    // N7c: imagens na resolucao interna para a apresentacao (anel; a apresentacao fixa a que esta desenhando)
    struct Slot {
        Image    image;
        uint32_t w = 0, h = 0;
        uint64_t seq = 0;      // render guardado (0 = vazio ou em escrita)
        int      pins = 0;     // apresentacoes em curso
        bool     shaderRead = false;  // ja escrito (GENERAL)
        uint32_t version = 0;  // muda quando a imagem e recriada
        VkSemaphore ready = VK_NULL_HANDLE;  // sinalizado pela escrita, esperado pela primeira apresentacao
        bool        readyPending = false;    // sinalizado e ainda nao esperado
    };
    static constexpr int kSlots = 4;
    Slot                 slots_[kSlots];
    std::mutex           slotMutex_;
    uint64_t             slotSeq_ = 0, lastSlot_ = 0;
    int                  writeSlot_ = -1;
    uint32_t             gameW_ = 0, gameH_ = 0;  // resolucao do jogo do quadro atual
    Buffer               down_;                   // a reducao na GPU (gameW_ x gameH_)
    VkDescriptorSetLayout downLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout     downPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule       downMod_ = VK_NULL_HANDLE;
    VkPipeline           downPipe_ = VK_NULL_HANDLE;
    VkDescriptorSet      downSet_ = VK_NULL_HANDLE;

    // layouts, pipelines e amostrador (fixos)
    VkDescriptorSetLayout texLayout_ = VK_NULL_HANDLE, fragLayout_ = VK_NULL_HANDLE, resolveLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      volPool_ = VK_NULL_HANDLE;  // N8.7
    VkDescriptorPool      texPool_ = VK_NULL_HANDLE, framePool_ = VK_NULL_HANDLE;
    VkPipelineLayout      gfxLayout_ = VK_NULL_HANDLE, compLayout_ = VK_NULL_HANDLE;
    VkShaderModule        vert_ = VK_NULL_HANDLE, frag_ = VK_NULL_HANDLE, fragList_ = VK_NULL_HANDLE, comp_ = VK_NULL_HANDLE;
    VkShaderModule        waterFrag_ = VK_NULL_HANDLE, waterFragList_ = VK_NULL_HANDLE;  // N9.7: -DWATER (o mesmo ambiente FP)
    VkPipeline            resolve_ = VK_NULL_HANDLE;
    VkSampler             sampler_ = VK_NULL_HANDLE, waterSampler_ = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, VkPipeline> pipes_;
    Tex                   dummy_;  // 1x1 para desenhos sem textura

    // alvo do quadro (recriado quando o tamanho muda)
    uint32_t        w_ = 0, h_ = 0;
    Image           color_, depth_, waterScene_;  // N9.7: cena opaca para reflexo/refracao da agua
    bool            waterSceneReady_ = false;
    VkRenderPass    pass_ = VK_NULL_HANDLE;
    VkFramebuffer   fb_ = VK_NULL_HANDLE;
    Buffer          heads_, frags_, readback_, headerRead_, verts_, staging_, tris_;
    Buffer          frameData_;  // N8.7: os parametros do quadro (fog), escritos pela CPU a cada render
    // N8.7: volumes modificadores (volumes.comp): o bit de sombra do pixel (anexo R8_UINT do passe das opacas), o segundo
    // passe (translucidas), os dados por quadro (volumes, triangulos, tiles, entradas) e o compute
    Image                 shadow_;
    VkRenderPass          passB_ = VK_NULL_HANDLE;
    VkFramebuffer         fbB_   = VK_NULL_HANDLE;
    Buffer                volVols_, volTris_, volTiles_, volEnt_;
    VkDescriptorSetLayout volLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet       volSet_    = VK_NULL_HANDLE;
    VkPipelineLayout      volPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        volMod_  = VK_NULL_HANDLE;
    VkPipeline            volPipe_ = VK_NULL_HANDLE;
    // N9.6 fase 1: oclusao ambiente (ao.comp): o AO do quadro (RGBA8, criado na primeira vez que liga), o conjunto e os
    // dois computes (o calculo e a media com a multiplicacao da cor)
    // N9.6 fase 3: a luz das fontes do catalogo (lights.comp): as luzes do quadro (na CPU), o buffer delas e os objetos do
    // passe, criados na primeira vez que ha luz
    struct LightGpu { float pos[4], col[4]; };
    static constexpr uint32_t kMaxLights = 16;
    std::vector<LightGpu> lights_;
    Buffer                lightBuf_;
    VkDescriptorSetLayout lightLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      lightPool_ = VK_NULL_HANDLE;
    VkDescriptorSet       lightSet_ = VK_NULL_HANDLE;
    VkPipelineLayout      lightPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        lightMod_ = VK_NULL_HANDLE;
    VkPipeline            lightPipe_ = VK_NULL_HANDLE;
    bool                  lightSetValid_ = false, lightFailed_ = false;
    std::atomic<bool>     lightOn_{false};
    // N9.6 fase 4: as sombras aprimoradas: a mascara dos volumes (volumes.comp -DMASK, com o seu proprio conjunto: o layout
    // fiel dos volumes nao muda) e a borda suave (soft_shadows.comp); as sombras de contato ficam no passe de luz
    std::atomic<bool>     shadowsOn_{false};
    Image                 shadowMask_;
    VkDescriptorSetLayout vmLayout_ = VK_NULL_HANDLE, softLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      vmPool_ = VK_NULL_HANDLE;
    VkDescriptorSet       vmSet_ = VK_NULL_HANDLE, softSet_ = VK_NULL_HANDLE;
    VkPipelineLayout      vmPipeLayout_ = VK_NULL_HANDLE, softPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        vmMod_ = VK_NULL_HANDLE, softMod_ = VK_NULL_HANDLE;
    VkPipeline            vmPipe_ = VK_NULL_HANDLE, softPipe_ = VK_NULL_HANDLE;
    bool                  vmSetValid_ = false, softSetValid_ = false, softFailed_ = false;
    Image                 aoSurf_;  // N9.6 fase 2b: o fator e o 1/w do opaco por pixel (rgba8 no alvo) para o catalogo
    bool                  aoSurfBound_ = false, aoSurfWanted_ = false, aoSurfFrame_ = false;
    Image                 aoImg_, aoHalf_;  // a alta usa aoImg_ (rgba8 no alvo); a media e a baixa, aoHalf_ (rgba32f, 1/4)
    VkDescriptorSetLayout aoLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      aoPool_   = VK_NULL_HANDLE;
    VkDescriptorSet       aoSet_    = VK_NULL_HANDLE;
    VkPipelineLayout      aoPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        aoMod_ = VK_NULL_HANDLE, aoBlurMod_ = VK_NULL_HANDLE;
    VkShaderModule        aoHalfMod_ = VK_NULL_HANDLE, aoUpMod_ = VK_NULL_HANDLE;
    VkShaderModule        aoBlurSurfMod_ = VK_NULL_HANDLE, aoUpSurfMod_ = VK_NULL_HANDLE;
    VkPipeline            aoBlurSurfPipe_ = VK_NULL_HANDLE, aoUpSurfPipe_ = VK_NULL_HANDLE;
    VkPipeline            aoPipe_ = VK_NULL_HANDLE, aoBlurPipe_ = VK_NULL_HANDLE, aoHalfPipe_ = VK_NULL_HANDLE, aoUpPipe_ = VK_NULL_HANDLE;
    bool                  aoSetValid_ = false, aoFailed_ = false;
    std::atomic<bool>     aoOn_{false};
    std::atomic<uint8_t>  aoQuality_{uint8_t(AoQuality::High)};  // o padrao do dispositivo e o da primeira versao
    uint32_t        fragCapacity_ = 0;
    VkDescriptorSet fragSet_ = VK_NULL_HANDLE, resolveSet_ = VK_NULL_HANDLE;

    std::unordered_map<uint32_t, Tex> texs_;
    uint32_t                          nextTex_ = 0;
    struct Upload {
        uint32_t tex;
        size_t   offset;
    };
    std::vector<Upload>   uploads_;
    std::vector<std::byte> uploadBytes_;
    PreparedFrame         prep_;
    uint32_t              imageW_ = 0, imageH_ = 0;
    std::vector<uint32_t> frameImage_;
    bool                  fromImage_ = false;
    // N9.7: a agua aprimorada (set_water): a qualidade e a fase pedidas; no quadro, a qualidade em uso, se ha desenho de
    // agua (waterWanted_), o horizonte e o fundo; a imagem da cena so existe com agua no quadro (waterScene_) e o descritor
    // do binding 5 aponta para ela ou para a textura neutra (waterBound_); sem memoria para ela, a agua fica original ate o
    // alvo mudar (waterFailed_)
    std::atomic<uint8_t>  waterQ_{0};
    std::atomic<uint64_t> waterRender_{0};
    WaterQuality          waterQuality_ = WaterQuality::Original;
    bool                  waterWanted_ = false, waterBound_ = false, waterFailed_ = false;
    uint64_t              waterFramesRec_ = 0, waterDrawsRec_ = 0;  // os da tentativa de gravacao em curso
    float                 waterTime_ = 0, waterHorizon_ = 0, waterBottom_ = 1;

    Error fail(const char* what, VkResult r = VK_SUCCESS) {  // motivo da falha para o diagnostico (stats().last_error)
        st_.last_error = std::string(what) + (r != VK_SUCCESS ? " (VkResult " + std::to_string(int(r)) + ")" : "");
        return Error::Io;
    }


    bool load(std::string* error) {
#ifdef _WIN32
        HMODULE m = LoadLibraryA("vulkan-1.dll");
        lib_      = m;
        if (m) f_.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(m, "vkGetInstanceProcAddr"));
#else
        lib_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (lib_) f_.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib_, "vkGetInstanceProcAddr"));
#endif
        if (!f_.gipa) {
            if (error) *error = "carregador do Vulkan nao encontrado (vulkan-1.dll)";
            return false;
        }
        f_.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(f_.gipa(VK_NULL_HANDLE, "vkCreateInstance"));
        return f_.vkCreateInstance != nullptr;
    }

    bool pick(std::string* error) {
        uint32_t n = 0;
        f_.vkEnumeratePhysicalDevices(inst_, &n, nullptr);
        std::vector<VkPhysicalDevice> devs(n);
        f_.vkEnumeratePhysicalDevices(inst_, &n, devs.data());
        std::string why = "nenhum dispositivo Vulkan";
        const char* want = std::getenv("BS_VULKAN_GPU");
        if (want && !*want) want = nullptr;
        for (int pass = 0; pass < 2 && !phys_; ++pass)
            for (VkPhysicalDevice d : devs) {
                VkPhysicalDeviceProperties p;
                f_.vkGetPhysicalDeviceProperties(d, &p);
                // BS_VULKAN_GPU=<parte do nome>: escolhe o dispositivo (testes por GPU); senao, discreta primeiro
                if (want && !std::strstr(p.deviceName, want)) continue;
                if (!want && pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;
                VkPhysicalDeviceFeatures ft;
                f_.vkGetPhysicalDeviceFeatures(d, &ft);
                if (!ft.depthClamp || !ft.fragmentStoresAndAtomics) {
                    why = std::string(p.deviceName) + " sem depthClamp/fragmentStoresAndAtomics";
                    continue;
                }
                VkFormatProperties fp;
                f_.vkGetPhysicalDeviceFormatProperties(d, VK_FORMAT_R8G8B8A8_UNORM, &fp);
                if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
                    why = std::string(p.deviceName) + " sem RGBA8 como imagem de armazenamento";
                    continue;
                }
                uint32_t nq = 0;
                f_.vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
                std::vector<VkQueueFamilyProperties> qs(nq);
                f_.vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qs.data());
                for (uint32_t q = 0; q < nq; ++q)
                    if ((qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qs[q].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                        phys_ = d, qfam_ = q, queueCount_ = qs[q].queueCount, st_.device = p.deviceName;
                        tsPeriod_ = qs[q].timestampValidBits ? p.limits.timestampPeriod : 0.f;
                        tsBits_   = qs[q].timestampValidBits;
                        st_.timestamps = tsPeriod_ > 0;
                        break;
                    }
                if (phys_) break;
            }
        if (!phys_) {
            if (error) *error = why;
            return false;
        }
        f_.vkGetPhysicalDeviceMemoryProperties(phys_, &mem_);
        {  // N7c: limite da resolucao interna
            VkPhysicalDeviceProperties p;
            f_.vkGetPhysicalDeviceProperties(phys_, &p);
            VkDeviceSize heap = 0;
            for (uint32_t i = 0; i < mem_.memoryHeapCount; ++i)
                if (mem_.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap = std::max(heap, mem_.memoryHeaps[i].size);
            // N9.6 fase 1: + 4 B/px da imagem do AO (rgba8 no alvo na alta; rgba32f em 1/4 na media e na baixa). A constante
            // de 96 B/px e a estimativa antiga (medidos ~268 B/px: a correcao fica no N9.12)
            // N9.7: + 4 B/px da imagem da cena para a agua (rgba8 no alvo, so com a agua ligada e um desenho de agua)
            const double pixels = double(heap) / 4.0 / (96.0 + 4.0 + 4.0);         // pixels do alvo que cabem
            const double byMem  = std::sqrt(pixels * 3.0 / 4.0);                    // altura 4:3
            const double byDim  = double(p.limits.maxImageDimension2D) * 3.0 / 4.0;  // a largura e 4/3 da altura
            st_.max_internal_height = uint32_t(std::max(480.0, std::min(byMem, byDim)));
        }
        return true;
    }


    bool layouts(std::string* error) {
        VkDescriptorSetLayoutBinding tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 1, li.pBindings = &tb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &texLayout_), "layout da textura", error)) return false;
        ++g_live;
        VkDescriptorSetLayoutBinding fb[7] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              // N8.7: os parametros do quadro (fog)
                                              {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              // N9.7: cena opaca para o material da agua
                                              {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              // N9.6 fase 2b: o AO do opaco para as translucidas do catalogo (5 fica
                                              // para a agua do N9.7)
                                              {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        li.bindingCount = 7, li.pBindings = fb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &fragLayout_), "layout das listas", error)) return false;
        ++g_live;
        VkDescriptorSetLayoutBinding rb[3] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        li.bindingCount = 3, li.pBindings = rb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &resolveLayout_), "layout do resolve", error)) return false;
        ++g_live;

        VkDescriptorPoolSize       tps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, pi.maxSets = 8192, pi.poolSizeCount = 1, pi.pPoolSizes = &tps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &pi, nullptr, &texPool_), "pool das texturas", error)) return false;
        ++g_live;
        VkDescriptorPoolSize fps[4] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
                                       {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
        pi.flags = 0, pi.maxSets = 3, pi.poolSizeCount = 4, pi.pPoolSizes = fps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &pi, nullptr, &framePool_), "pool do quadro", error)) return false;
        ++g_live;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = framePool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &fragLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &fragSet_), "conjunto das listas", error)) return false;
        ai.pSetLayouts = &resolveLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &resolveSet_), "conjunto do resolve", error)) return false;
        {  // N8.7: volumes.comp
            VkDescriptorSetLayoutBinding vb[7] = {
                {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
            VkDescriptorSetLayoutCreateInfo vli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            vli.bindingCount = 7, vli.pBindings = vb;
            if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &vli, nullptr, &volLayout_), "layout dos volumes", error)) return false;
            ++g_live;
            VkDescriptorPoolSize       vps[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
                                                 {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
            VkDescriptorPoolCreateInfo vpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            vpi.maxSets = 1, vpi.poolSizeCount = 3, vpi.pPoolSizes = vps;
            if (!ok(f_.vkCreateDescriptorPool(dev_, &vpi, nullptr, &volPool_), "pool dos volumes", error)) return false;
            ++g_live;
            VkDescriptorSetAllocateInfo vai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            vai.descriptorPool = volPool_, vai.descriptorSetCount = 1, vai.pSetLayouts = &volLayout_;
            if (!ok(f_.vkAllocateDescriptorSets(dev_, &vai, &volSet_), "conjunto dos volumes", error)) return false;
            VkPushConstantRange        vpr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 36};
            VkPipelineLayoutCreateInfo vl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            vl.setLayoutCount = 1, vl.pSetLayouts = &volLayout_, vl.pushConstantRangeCount = 1, vl.pPushConstantRanges = &vpr;
            if (!ok(f_.vkCreatePipelineLayout(dev_, &vl, nullptr, &volPipeLayout_), "layout dos volumes", error)) return false;
            ++g_live;
            volMod_ = floatControls_ && denormPreserve_ ? module(kVolumesCompFcd, sizeof kVolumesCompFcd)
                      : floatControls_                  ? module(kVolumesCompFc, sizeof kVolumesCompFc)
                                                        : module(kVolumesComp, sizeof kVolumesComp);
            if (!volMod_) {
                if (error) *error = "shader dos volumes";
                return false;
            }
            VkComputePipelineCreateInfo vc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            vc.stage  = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, volMod_, "main", nullptr};
            vc.layout = volPipeLayout_;
            if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &vc, nullptr, &volPipe_), "pipeline dos volumes", error))
                return false;
            ++g_live;
        }
        // N7c: reducao da resolucao interna (downsample.comp): imagem de armazenamento -> buffer
        VkDescriptorSetLayoutBinding db[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        li.bindingCount = 2, li.pBindings = db;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &downLayout_), "layout da reducao", error)) return false;
        ++g_live;
        ai.pSetLayouts = &downLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &downSet_), "conjunto da reducao", error)) return false;

        VkPushConstantRange        pr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};  // 80 <= 128
        VkDescriptorSetLayout      sets[2] = {texLayout_, fragLayout_};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 2, pl.pSetLayouts = sets, pl.pushConstantRangeCount = 1, pl.pPushConstantRanges = &pr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &pl, nullptr, &gfxLayout_), "layout grafico", error)) return false;
        ++g_live;
        VkPushConstantRange cpr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
        pl.setLayoutCount = 1, pl.pSetLayouts = &resolveLayout_, pl.pPushConstantRanges = &cpr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &pl, nullptr, &compLayout_), "layout do resolve", error)) return false;
        ++g_live;

        vert_     = module(kPvrVert, sizeof kPvrVert);
        // a variante com o ambiente FP que o dispositivo garante: RTE + subnormais, so RTE, ou o padrao
        if (floatControls_ && denormPreserve_) {
            frag_     = module(kPvrFragFcd, sizeof kPvrFragFcd);
            fragList_ = module(kPvrFragAutosortFcd, sizeof kPvrFragAutosortFcd);
            waterFrag_     = module(kPvrFragFcdWater, sizeof kPvrFragFcdWater);
            waterFragList_ = module(kPvrFragAutosortFcdWater, sizeof kPvrFragAutosortFcdWater);
            comp_     = module(kResolveCompFcd, sizeof kResolveCompFcd);
        } else if (floatControls_) {
            frag_     = module(kPvrFragFc, sizeof kPvrFragFc);
            fragList_ = module(kPvrFragAutosortFc, sizeof kPvrFragAutosortFc);
            waterFrag_     = module(kPvrFragFcWater, sizeof kPvrFragFcWater);
            waterFragList_ = module(kPvrFragAutosortFcWater, sizeof kPvrFragAutosortFcWater);
            comp_     = module(kResolveCompFc, sizeof kResolveCompFc);
        } else {
            frag_     = module(kPvrFrag, sizeof kPvrFrag);
            fragList_ = module(kPvrFragAutosort, sizeof kPvrFragAutosort);
            waterFrag_     = module(kPvrFragWater, sizeof kPvrFragWater);
            waterFragList_ = module(kPvrFragAutosortWater, sizeof kPvrFragAutosortWater);
            comp_     = module(kResolveComp, sizeof kResolveComp);
        }
        if (!vert_ || !frag_ || !fragList_ || !comp_ || !waterFrag_ || !waterFragList_) {
            if (error) *error = "shaders";
            return false;
        }
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, comp_, "main", nullptr};
        cp.layout = compLayout_;
        if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cp, nullptr, &resolve_), "pipeline do resolve", error))
            return false;
        ++g_live;
        {
            VkPushConstantRange        dpr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
            VkPipelineLayoutCreateInfo dl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            dl.setLayoutCount = 1, dl.pSetLayouts = &downLayout_, dl.pushConstantRangeCount = 1, dl.pPushConstantRanges = &dpr;
            if (!ok(f_.vkCreatePipelineLayout(dev_, &dl, nullptr, &downPipeLayout_), "layout da reducao", error)) return false;
            ++g_live;
            downMod_ = module(kDownsampleComp, sizeof kDownsampleComp);
            if (!downMod_) {
                if (error) *error = "shader da reducao";
                return false;
            }
            VkComputePipelineCreateInfo dc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            dc.stage  = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, downMod_, "main", nullptr};
            dc.layout = downPipeLayout_;
            if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &dc, nullptr, &downPipe_), "pipeline da reducao", error))
                return false;
            ++g_live;
        }

        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;  // texelFetch: a amostragem e a do shader
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (!ok(f_.vkCreateSampler(dev_, &si, nullptr, &sampler_), "amostrador", error)) return false;
        ++g_live;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        if (!ok(f_.vkCreateSampler(dev_, &si, nullptr, &waterSampler_), "amostrador da agua", error)) return false;
        ++g_live;

        TextureDesc d;
        d.width = d.height = 1;
        const uint32_t white = 0xFFFFFFFF;
        auto r = create_texture(d, std::as_bytes(std::span<const uint32_t>(&white, 1)));
        if (!r) {
            if (error) *error = "textura vazia";
            return false;
        }
        dummy_ = texs_[r.value().value];
        texs_.erase(r.value().value);
        dummyId_ = r.value().value;
        return true;
    }
    uint32_t dummyId_ = 0;

    bool fragments(uint32_t capacity) {
        MemScope mc(telemetry::Mem::Targets);  // N9.5 fase 5C
        freeBuffer(frags_);
        frags_ = makeBuffer(VkDeviceSize(capacity) * kFragBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
        if (!frags_.buf) return false;
        fragCapacity_ = capacity;
        writeFrameSets();
        return true;
    }

    void writeFrameSets() {
        VkDescriptorBufferInfo fbi{frags_.buf, 0, VK_WHOLE_SIZE}, hbi{heads_.buf, 0, VK_WHOLE_SIZE}, tbi{tris_.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo pbi{frameData_.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo  ii{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo  di{VK_NULL_HANDLE, depth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        // N9.7: a cena para a agua, quando existe; senao a textura neutra (so a variante -DWATER a amostra, e so com a imagem)
        VkDescriptorImageInfo  wi = waterScene_.img
                                        ? VkDescriptorImageInfo{waterSampler_, waterScene_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
                                        : VkDescriptorImageInfo{waterSampler_, dummy_.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // N9.6 fase 2b: a imagem do AO para o catalogo, quando existe; senao a textura neutra (a amostra so acontece com
        // F_AO_SURF, que so liga com a imagem escrita no quadro)
        VkDescriptorImageInfo  si = aoSurf_.img ? VkDescriptorImageInfo{sampler_, aoSurf_.view, VK_IMAGE_LAYOUT_GENERAL}
                                                : VkDescriptorImageInfo{sampler_, dummy_.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet   w[10] = {};
        for (int i = 0; i < 10; ++i) w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, w[i].descriptorCount = 1;
        w[9].dstSet = fragSet_, w[9].dstBinding = 6, w[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[9].pImageInfo = &si;
        w[7].dstSet = fragSet_, w[7].dstBinding = 4, w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[7].pBufferInfo = &pbi;
        w[6].dstSet = fragSet_, w[6].dstBinding = 3, w[6].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, w[6].pImageInfo = &di;
        w[5].dstSet = fragSet_, w[5].dstBinding = 2, w[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[5].pBufferInfo = &tbi;
        w[0].dstSet = fragSet_, w[0].dstBinding = 0, w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[0].pBufferInfo = &fbi;
        w[1].dstSet = fragSet_, w[1].dstBinding = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[1].pBufferInfo = &hbi;
        w[2].dstSet = resolveSet_, w[2].dstBinding = 0, w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[2].pBufferInfo = &fbi;
        w[3].dstSet = resolveSet_, w[3].dstBinding = 1, w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[3].pBufferInfo = &hbi;
        w[4].dstSet = resolveSet_, w[4].dstBinding = 2, w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, w[4].pImageInfo = &ii;
        w[8].dstSet = fragSet_, w[8].dstBinding = 5, w[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        w[8].pImageInfo = &wi;
        // a posicao 5 (triangulos) so existe depois do primeiro quadro: ate la, sem ela
        VkWriteDescriptorSet list[10];
        uint32_t             n = 0;
        for (int i = 0; i < 10; ++i)
            if ((i != 5 || tris_.buf) && (i != 7 || frameData_.buf) && (i != 9 || aoSurf_.img || dummy_.image.view) &&
                (i != 8 || waterScene_.img || dummy_.image.view))
                list[n++] = w[i];
        f_.vkUpdateDescriptorSets(dev_, n, list, 0, nullptr);
        aoSurfBound_ = aoSurf_.img != VK_NULL_HANDLE;
        waterBound_  = waterScene_.img != VK_NULL_HANDLE;  // N9.7
        ++st_.frame_set_writes;
    }

    // alvo w x h: cor RGBA8 (anexo, armazenamento do resolve e dos volumes, origem da leitura), profundidade D32 (amostrada
    // pelos volumes), o bit de sombra (R8_UINT), cabecas das listas por pixel, leitura de volta.
    // N8.7: dois passes. A: opacas e punch-through (cor, profundidade e o bit de sombra do poligono visivel). Entre eles, os
    // volumes modificadores (volumes.comp, so com volumes no quadro). B: subpasso 0 translucidas na ordem; subpasso 1
    // translucidas em autosort (a profundidade como entrada, o >= feito no shader).
    bool target(uint32_t w, uint32_t h, uint32_t gw, uint32_t gh) {
        gameW_ = gw, gameH_ = gh;
        if (w == w_ && h == h_ && gw == downW_ && gh == downH_ && pass_) return true;
        MemScope mc(telemetry::Mem::Targets);  // N9.5 fase 5C
        waitIdle();
        freeTarget();
        w_ = w, h_ = h, downW_ = gw, downH_ = gh;
        color_ = makeImage(w, h, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT);
        depth_ = makeImage(w, h, VK_FORMAT_D32_SFLOAT,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT);
        shadow_ = makeImage(w, h, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
        if (!color_.img || !depth_.img || !shadow_.img) return false;
        {  // passe A
            VkAttachmentDescription at[3] = {};
            at[0].format = VK_FORMAT_R8G8B8A8_UNORM, at[0].samples = VK_SAMPLE_COUNT_1_BIT;
            at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, at[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
            at[1] = at[0], at[1].format = VK_FORMAT_D32_SFLOAT, at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            at[2] = at[0], at[2].format = VK_FORMAT_R8_UINT, at[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkAttachmentReference cr[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
            VkAttachmentReference dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkSubpassDescription  sp{};
            sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sp.colorAttachmentCount = 2, sp.pColorAttachments = cr;
            sp.pDepthStencilAttachment = &dr;
            VkSubpassDependency dep[2] = {};
            dep[0].srcSubpass = VK_SUBPASS_EXTERNAL, dep[0].dstSubpass = 0;
            dep[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                   VK_ACCESS_SHADER_READ_BIT;
            dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            // as escritas do passe A: lidas pelos volumes (compute), pelo passe B e pelas copias
            dep[1].srcSubpass = 0, dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_SHADER_WRITE_BIT;
            dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
            VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rp.attachmentCount = 3, rp.pAttachments = at, rp.subpassCount = 1, rp.pSubpasses = &sp;
            rp.dependencyCount = 2, rp.pDependencies = dep;
            if (f_.vkCreateRenderPass(dev_, &rp, nullptr, &pass_) != VK_SUCCESS) return false;
            ++g_live;
            VkImageView             views[3] = {color_.view, depth_.view, shadow_.view};
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = pass_, fi.attachmentCount = 3, fi.pAttachments = views, fi.width = w, fi.height = h, fi.layers = 1;
            if (f_.vkCreateFramebuffer(dev_, &fi, nullptr, &fb_) != VK_SUCCESS) return false;
            ++g_live;
        }
        {  // passe B
            VkAttachmentDescription at[2] = {};
            at[0].format = VK_FORMAT_R8G8B8A8_UNORM, at[0].samples = VK_SAMPLE_COUNT_1_BIT;
            at[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            at[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL, at[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
            at[1] = at[0], at[1].format = VK_FORMAT_D32_SFLOAT, at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            at[1].initialLayout = at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkAttachmentReference ir{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            VkSubpassDescription  sps[2] = {};
            sps[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sps[0].colorAttachmentCount = 1, sps[0].pColorAttachments = &cr;
            sps[0].pDepthStencilAttachment = &dr;
            sps[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, sps[1].colorAttachmentCount = 1, sps[1].pColorAttachments = &cr;
            sps[1].inputAttachmentCount = 1, sps[1].pInputAttachments = &ir;
            VkSubpassDependency dep[3] = {};
            dep[0].srcSubpass = VK_SUBPASS_EXTERNAL, dep[0].dstSubpass = 0;
            dep[0].srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            dep[2].srcSubpass = 0, dep[2].dstSubpass = 1, dep[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            dep[2].srcStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            // o store da profundidade no fim do subpasso 1 (mesmo DONT_CARE) e escrita em LATE_FRAGMENT_TESTS: entra no
            // destino para ficar ordenado depois da transicao de layout desta dependencia (SYNC-HAZARD-WRITE-AFTER-WRITE)
            dep[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dep[2].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_SHADER_WRITE_BIT;
            dep[2].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dep[1].srcSubpass = 1, dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dep[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[1].dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
            VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rp.attachmentCount = 2, rp.pAttachments = at, rp.subpassCount = 2, rp.pSubpasses = sps;
            rp.dependencyCount = 3, rp.pDependencies = dep;
            if (f_.vkCreateRenderPass(dev_, &rp, nullptr, &passB_) != VK_SUCCESS) return false;
            ++g_live;
            VkImageView             views[2] = {color_.view, depth_.view};
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = passB_, fi.attachmentCount = 2, fi.pAttachments = views, fi.width = w, fi.height = h, fi.layers = 1;
            if (f_.vkCreateFramebuffer(dev_, &fi, nullptr, &fbB_) != VK_SUCCESS) return false;
            ++g_live;
        }
        heads_      = makeBuffer(kHeader + VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
        // a leitura de volta e sempre na resolucao do jogo: com a escala, a reducao feita na GPU (down_)
        readback_   = makeBuffer(VkDeviceSize(gw) * gh * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        if (w != gw || h != gh) {
            down_ = makeBuffer(VkDeviceSize(gw) * gh * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
            if (!down_.buf) return false;
            VkDescriptorImageInfo  ii{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo bi{down_.buf, 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet   dw[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
            dw[0].dstSet = downSet_, dw[0].dstBinding = 0, dw[0].descriptorCount = 1, dw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            dw[0].pImageInfo = &ii;
            dw[1].dstSet = downSet_, dw[1].dstBinding = 1, dw[1].descriptorCount = 1, dw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            dw[1].pBufferInfo = &bi;
            f_.vkUpdateDescriptorSets(dev_, 2, dw, 0, nullptr);
        }
        headerRead_ = makeBuffer(kHeader, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        if (!frameData_.buf) frameData_ = makeBuffer(kFrameDataBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        if (!heads_.buf || !readback_.buf || !headerRead_.buf || !frameData_.buf) return false;
        for (auto& [k, p] : pipes_) f_.vkDestroyPipeline(dev_, p, nullptr), --g_live;  // o passe mudou
        pipes_.clear();
        volSetValid_ = false;  // as imagens mudaram: o conjunto dos volumes e regravado no proximo quadro com volumes
        // 4 fragmentos por pixel na resolucao do jogo; com a escala, 2 (o quadro cresce sozinho se faltar)
        return fragments(std::max(fragCapacity_, w * h * (scale_ == 1.f ? 4u : 2u)));
    }

    // N8.7: o conjunto dos volumes (volumes.comp). Regravado sempre que um buffer dele ou o alvo muda (o handle novo pode
    // ser igual ao liberado: nunca comparar handles)
    bool volSetValid_ = false;
    // N9.6 fase 4: os objetos das sombras aprimoradas nascem na primeira vez que ha volume com elas ligadas; uma falha
    // desliga so a borda suave (os volumes seguem pelo caminho fiel)
    bool softObjects() {
        if (vmPipe_ && softPipe_) return true;
        if (softFailed_) return false;
        std::string err;
        auto fail_s = [&](const std::string& why) {
            softFailed_ = true;
            std::fprintf(stderr, "[render-vk] sombras aprimoradas desligadas: %s\n", why.c_str());
            return false;
        };
        VkDescriptorSetLayoutBinding vb[8] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 8, li.pBindings = vb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &vmLayout_), "layout da mascara", &err)) return fail_s(err);
        ++g_live;
        VkDescriptorSetLayoutBinding sb[4] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        li.bindingCount = 4, li.pBindings = sb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &softLayout_), "layout da borda", &err)) return fail_s(err);
        ++g_live;
        VkDescriptorPoolSize       ps[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
                                            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 2, pi.poolSizeCount = 3, pi.pPoolSizes = ps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &pi, nullptr, &vmPool_), "pool das sombras", &err)) return fail_s(err);
        ++g_live;
        VkDescriptorSetLayout       sl[2] = {vmLayout_, softLayout_};
        VkDescriptorSet             ss[2] = {};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = vmPool_, ai.descriptorSetCount = 2, ai.pSetLayouts = sl;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, ss), "conjuntos das sombras", &err)) return fail_s(err);
        vmSet_ = ss[0], softSet_ = ss[1];
        VkPushConstantRange        vpr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 36}, spr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1, pl.pSetLayouts = &vmLayout_, pl.pushConstantRangeCount = 1, pl.pPushConstantRanges = &vpr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &pl, nullptr, &vmPipeLayout_), "layout da mascara", &err)) return fail_s(err);
        ++g_live;
        pl.pSetLayouts = &softLayout_, pl.pPushConstantRanges = &spr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &pl, nullptr, &softPipeLayout_), "layout da borda", &err)) return fail_s(err);
        ++g_live;
        // o mesmo ambiente FP do caminho fiel (a cobertura dos volumes e a mesma)
        vmMod_ = floatControls_ && denormPreserve_ ? module(kVolumesMaskCompFcd, sizeof kVolumesMaskCompFcd)
                 : floatControls_                  ? module(kVolumesMaskCompFc, sizeof kVolumesMaskCompFc)
                                                   : module(kVolumesMaskComp, sizeof kVolumesMaskComp);
        softMod_ = module(kSoftShadowsComp, sizeof kSoftShadowsComp);
        if (!vmMod_ || !softMod_) return fail_s("shaders das sombras");
        for (auto [mod, lay, pipe] : {std::tuple{vmMod_, vmPipeLayout_, &vmPipe_}, std::tuple{softMod_, softPipeLayout_, &softPipe_}}) {
            VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            cp.stage  = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr};
            cp.layout = lay;
            if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cp, nullptr, pipe), "pipeline das sombras", &err))
                return fail_s(err);
            ++g_live;
        }
        return true;
    }

    void writeSoftSets() {
        VkDescriptorBufferInfo b[4] = {{volVols_.buf, 0, VK_WHOLE_SIZE}, {volTris_.buf, 0, VK_WHOLE_SIZE},
                                       {volTiles_.buf, 0, VK_WHOLE_SIZE}, {volEnt_.buf, 0, VK_WHOLE_SIZE}};
        VkDescriptorImageInfo  di{sampler_, depth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  si{sampler_, shadow_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  ci{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo  mi{VK_NULL_HANDLE, shadowMask_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet   w[12] = {};
        for (uint32_t i = 0; i < 12; ++i) w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, w[i].descriptorCount = 1;
        for (uint32_t i = 0; i < 8; ++i) {
            w[i].dstSet = vmSet_, w[i].dstBinding = i;
            if (i < 4) w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[i].pBufferInfo = &b[i];
        }
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[4].pImageInfo = &di;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[5].pImageInfo = &si;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, w[6].pImageInfo = &ci;
        w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, w[7].pImageInfo = &mi;
        for (uint32_t i = 0; i < 4; ++i) w[8 + i].dstSet = softSet_, w[8 + i].dstBinding = i;
        w[8].descriptorType = w[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[8].pImageInfo = &di, w[9].pImageInfo = &si;
        w[10].descriptorType = w[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[10].pImageInfo = &ci, w[11].pImageInfo = &mi;
        f_.vkUpdateDescriptorSets(dev_, 12, w, 0, nullptr);
        vmSetValid_ = softSetValid_ = true;
    }

    void writeVolSet() {
        VkDescriptorBufferInfo b[4] = {{volVols_.buf, 0, VK_WHOLE_SIZE}, {volTris_.buf, 0, VK_WHOLE_SIZE},
                                       {volTiles_.buf, 0, VK_WHOLE_SIZE}, {volEnt_.buf, 0, VK_WHOLE_SIZE}};
        VkDescriptorImageInfo  di{sampler_, depth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  si{sampler_, shadow_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  ci{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet   w[7] = {};
        for (uint32_t i = 0; i < 7; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, w[i].dstSet = volSet_, w[i].dstBinding = i, w[i].descriptorCount = 1;
            if (i < 4) w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[i].pBufferInfo = &b[i];
        }
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[4].pImageInfo = &di;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[5].pImageInfo = &si;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, w[6].pImageInfo = &ci;
        f_.vkUpdateDescriptorSets(dev_, 7, w, 0, nullptr);
        volSetValid_ = true;
    }

    // N9.6 fase 1 (revisao Codex 20261110-113738): os objetos do AO nascem na primeira vez que ele liga; uma falha aqui
    // desliga so o AO (o renderizador fiel continua) e fica no log uma vez. O que ja foi criado sai na destruicao.
    bool aoObjects() {
        if (aoPipe_ && aoBlurPipe_ && aoHalfPipe_ && aoUpPipe_ && aoBlurSurfPipe_ && aoUpSurfPipe_) return true;
        if (aoFailed_) return false;
        std::string err;
        auto fail_ao = [&](const std::string& why) {
            aoFailed_ = true;
            std::fprintf(stderr, "[render-vk] oclusao ambiente desligada: %s\n", why.c_str());
            return false;
        };
        // 3: o AO no alvo (alta); 4: o AO em meia resolucao (media e baixa). Cada pipeline usa so a sua: a outra imagem pode
        // nem existir (so a da qualidade escolhida e criada)
        VkDescriptorSetLayoutBinding ab[6] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            // N9.6 fase 2b: o fator e o 1/w do opaco para as translucidas do catalogo (so as variantes SURF)
            {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ali{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ali.bindingCount = 6, ali.pBindings = ab;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &ali, nullptr, &aoLayout_), "layout do AO", &err)) return fail_ao(err);
        ++g_live;
        VkDescriptorPoolSize       aps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}};
        VkDescriptorPoolCreateInfo api{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        api.maxSets = 1, api.poolSizeCount = 2, api.pPoolSizes = aps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &api, nullptr, &aoPool_), "pool do AO", &err)) return fail_ao(err);
        ++g_live;
        VkDescriptorSetAllocateInfo aai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        aai.descriptorPool = aoPool_, aai.descriptorSetCount = 1, aai.pSetLayouts = &aoLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &aai, &aoSet_), "conjunto do AO", &err)) return fail_ao(err);
        VkPushConstantRange        apr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 64};
        VkPipelineLayoutCreateInfo al{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        al.setLayoutCount = 1, al.pSetLayouts = &aoLayout_, al.pushConstantRangeCount = 1, al.pPushConstantRanges = &apr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &al, nullptr, &aoPipeLayout_), "layout do AO", &err)) return fail_ao(err);
        ++g_live;
        aoMod_ = module(kAoComp, sizeof kAoComp), aoBlurMod_ = module(kAoBlurComp, sizeof kAoBlurComp);
        aoHalfMod_ = module(kAoHalfComp, sizeof kAoHalfComp), aoUpMod_ = module(kAoUpComp, sizeof kAoUpComp);
        aoBlurSurfMod_ = module(kAoBlurSurfComp, sizeof kAoBlurSurfComp), aoUpSurfMod_ = module(kAoUpSurfComp, sizeof kAoUpSurfComp);
        if (!aoMod_ || !aoBlurMod_ || !aoHalfMod_ || !aoUpMod_ || !aoBlurSurfMod_ || !aoUpSurfMod_) {
            return fail_ao("shader do AO");
        }
        for (auto [mod, pipe] : {std::pair{aoMod_, &aoPipe_}, std::pair{aoBlurMod_, &aoBlurPipe_},
                                 std::pair{aoHalfMod_, &aoHalfPipe_}, std::pair{aoUpMod_, &aoUpPipe_},
                                 std::pair{aoBlurSurfMod_, &aoBlurSurfPipe_}, std::pair{aoUpSurfMod_, &aoUpSurfPipe_}}) {
            VkComputePipelineCreateInfo ac{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            ac.stage  = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr};
            ac.layout = aoPipeLayout_;
            if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &ac, nullptr, pipe), "pipeline do AO", &err)) return fail_ao(err);
            ++g_live;
        }
        return true;
    }

    // N9.6 fase 3: os objetos da luz nascem na primeira vez que ha luz; uma falha desliga so a luz (fica no log uma vez)
    bool lightObjects() {
        if (lightPipe_) return true;
        if (lightFailed_) return false;
        std::string err;
        auto fail_l = [&](const std::string& why) {
            lightFailed_ = true;
            std::fprintf(stderr, "[render-vk] luz das fontes desligada: %s\n", why.c_str());
            return false;
        };
        VkDescriptorSetLayoutBinding lb[4] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 4, li.pBindings = lb;
        if (!ok(f_.vkCreateDescriptorSetLayout(dev_, &li, nullptr, &lightLayout_), "layout da luz", &err)) return fail_l(err);
        ++g_live;
        VkDescriptorPoolSize       ps[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                                            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1, pi.poolSizeCount = 3, pi.pPoolSizes = ps;
        if (!ok(f_.vkCreateDescriptorPool(dev_, &pi, nullptr, &lightPool_), "pool da luz", &err)) return fail_l(err);
        ++g_live;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = lightPool_, ai.descriptorSetCount = 1, ai.pSetLayouts = &lightLayout_;
        if (!ok(f_.vkAllocateDescriptorSets(dev_, &ai, &lightSet_), "conjunto da luz", &err)) return fail_l(err);
        VkPushConstantRange        pr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 48};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1, pl.pSetLayouts = &lightLayout_, pl.pushConstantRangeCount = 1, pl.pPushConstantRanges = &pr;
        if (!ok(f_.vkCreatePipelineLayout(dev_, &pl, nullptr, &lightPipeLayout_), "layout da luz", &err)) return fail_l(err);
        ++g_live;
        lightMod_ = module(kLightsComp, sizeof kLightsComp);
        if (!lightMod_) return fail_l("shader da luz");
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage  = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, lightMod_, "main", nullptr};
        cp.layout = lightPipeLayout_;
        if (!ok(f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cp, nullptr, &lightPipe_), "pipeline da luz", &err))
            return fail_l(err);
        ++g_live;
        return true;
    }

    void writeLightSet() {
        VkDescriptorImageInfo  di{sampler_, depth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  si{sampler_, shadow_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  ci{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo bi{lightBuf_.buf, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet   w[4] = {};
        for (uint32_t i = 0; i < 4; ++i)
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, w[i].dstSet = lightSet_, w[i].dstBinding = i, w[i].descriptorCount = 1;
        w[0].descriptorType = w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[0].pImageInfo = &di, w[1].pImageInfo = &si;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, w[2].pImageInfo = &ci;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, w[3].pBufferInfo = &bi;
        f_.vkUpdateDescriptorSets(dev_, 4, w, 0, nullptr);
        lightSetValid_ = true;
    }

    // N9.6 fase 3: as luzes do quadro, antes de gravar os comandos: cada desenho de uma fonte do catalogo vira uma luz no
    // centro dele, na camera pela projecao do Ninja (cx, cy no centro do quadro do jogo, dist 500, z = 1 / (1/w)); as que
    // ficam a menos de kMerge uma da outra viram uma (a media pesada pelos vertices); no maximo kMaxLights
    void gatherLights(const CommandList& cl) {
        lights_.clear();
        const FrameParams& fr = cl.frame;
        if (!lightOn_.load(std::memory_order_relaxed) || fr.height != 480 || fr.width < 640 || lightFailed_) return;
        constexpr float kRadius = 400.f, kIntensity = 1.f, kMerge = 80.f;
        std::vector<float> weight;
        const float        cx = float(fr.width) / 2.f, cy = float(fr.height) / 2.f, dist = 500.f;
        for (const Draw& d : cl.draws) {
            if (!d.state.texture || d.first_vertex > cl.vertices.size() || d.vertex_count > cl.vertices.size() - d.first_vertex)
                continue;
            auto it = texs_.find(d.state.texture.value);
            if (it == texs_.end() || !(it->second.material & kMaterialEmissive)) continue;
            double sx = 0, sy = 0, sz = 0;
            uint32_t n = 0;
            for (uint32_t k = 0; k < d.vertex_count; ++k) {
                const Vertex& v = cl.vertices[d.first_vertex + k];
                if (!std::isfinite(v.inv_w) || !(v.inv_w > 0) || !std::isfinite(v.x) || !std::isfinite(v.y)) continue;
                const double z = 1.0 / double(v.inv_w);
                if (!(z > 1.5)) continue;  // o plano proximo (fase 0, 2.3): sem posicao confiavel
                sx += (v.x - cx) * z / dist, sy += (v.y - cy) * z / dist, sz += z, ++n;
            }
            if (!n) continue;
            const float    px = float(sx / n), py = float(sy / n), pz = float(sz / n);
            const uint32_t rgb = it->second.light;
            size_t         j   = 0;
            for (; j < lights_.size(); ++j) {
                const float dx = lights_[j].pos[0] - px, dy = lights_[j].pos[1] - py, dz = lights_[j].pos[2] - pz;
                if (dx * dx + dy * dy + dz * dz < kMerge * kMerge) break;
            }
            if (j < lights_.size()) {  // a mesma luz (as partes de uma lampada): a media pesada pelos vertices
                const float w0 = weight[j], w1 = float(n), t = w1 / (w0 + w1);
                for (int c = 0; c < 3; ++c) lights_[j].pos[c] += (c == 0 ? px : c == 1 ? py : pz) * t - lights_[j].pos[c] * t;
                weight[j] = w0 + w1;
                continue;
            }
            if (lights_.size() >= kMaxLights) continue;
            LightGpu l{{px, py, pz, kRadius},
                       {float((rgb >> 16) & 0xFF) / 255.f, float((rgb >> 8) & 0xFF) / 255.f, float(rgb & 0xFF) / 255.f, kIntensity}};
            lights_.push_back(l), weight.push_back(float(n));
        }
        if (lights_.empty()) return;
        if (!lightBuf_.buf) {
            if (!ensureHost(lightBuf_, sizeof(LightGpu) * kMaxLights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
                lights_.clear();
                return;
            }
            lightSetValid_ = false;
        }
        std::memcpy(lightBuf_.map, lights_.data(), lights_.size() * sizeof(LightGpu));
    }

    // N9.6 fase 3: o passe da luz das fontes, depois do AO e antes dos volumes (so com luz no quadro)
    bool lighting(const CommandList& cl) {
        if (lights_.empty() || !lightObjects()) return true;
        if (!lightSetValid_) writeLightSet();
        const FrameParams& fr = cl.frame;
        struct {
            uint32_t w, h;
            float    invScale, cx, cy, dist;
            uint32_t raw;
            float    bias, depthScale;
            uint32_t count;
            float    strength;
            uint32_t pad;
        } pc{w_, h_, 1.f / scale_, float(fr.width) / 2.f, float(fr.height) / 2.f, 500.f, rawDepth_ ? 1u : 0u, prep_.depth_bias,
             prep_.depth_scale, uint32_t(lights_.size()), 0.6f, shadowsOn_.load(std::memory_order_relaxed) ? 1u : 0u};
        static_assert(sizeof pc == 48);
        f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, lightPipeLayout_, 0, 1, &lightSet_, 0, nullptr);
        f_.vkCmdPushConstants(cmd_, lightPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
        f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, lightPipe_);
        f_.vkCmdDispatch(cmd_, (w_ + 7) / 8, (h_ + 7) / 8, 1);
        imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        ++st_.light_frames, st_.lights_max = std::max(st_.lights_max, uint32_t(lights_.size()));
        return true;
    }

    void writeAoSet() {
        VkDescriptorImageInfo di{sampler_, depth_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo si{sampler_, shadow_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo ci{VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo ai{VK_NULL_HANDLE, aoImg_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo hi{VK_NULL_HANDLE, aoHalf_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo ui{VK_NULL_HANDLE, aoSurf_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet  w[6] = {};
        const VkDescriptorImageInfo* inf[6] = {&di, &si, &ci, &ai, &hi, &ui};
        uint32_t                     n      = 0;
        for (uint32_t i = 0; i < 6; ++i) {
            // so a imagem da qualidade escolhida; a 5 so com uma translucida do catalogo
            if ((i == 3 && !aoImg_.img) || (i == 4 && !aoHalf_.img) || (i == 5 && !aoSurf_.img)) continue;
            w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, w[n].dstSet = aoSet_, w[n].dstBinding = i, w[n].descriptorCount = 1;
            w[n].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w[n].pImageInfo     = inf[i];
            ++n;
        }
        f_.vkUpdateDescriptorSets(dev_, n, w, 0, nullptr);
        aoSetValid_ = true;
    }

    // N9.6 fase 1 (docs/N9-6-AUDITORIA.md): oclusao ambiente entre o passe A e os volumes, so quando ligada
    // (set_ambient_occlusion: a Remastered com o Vulkan, no render do framebuffer 640x480 ou do quadro largo). A posicao na
    // camera sai da profundidade pela projecao do Ninja: cx e cy no centro do quadro do jogo (o quadro largo desloca a IR e o
    // centro junto: 427 em 854), dist 500 (o gameplay; o zoom do jogo muda a distancia em poucos por cento, o raio do AO
    // muda junto). Desligada, nada muda: a imagem e a mesma de antes do N9.6.
    bool ambientOcclusion(const CommandList& cl) {
        const FrameParams& fr = cl.frame;
        if (!aoOn_.load(std::memory_order_relaxed)) {  // desligado: a memoria do AO volta (o quadro anterior ja terminou)
            if (aoImg_.img || aoHalf_.img) freeImage(aoImg_), freeImage(aoHalf_), aoSetValid_ = false;
            st_.ao_image_bytes = 0;
            return true;
        }
        if (fr.height != 480 || fr.width < 640) return true;
        if (!aoObjects()) return true;  // sem os objetos: o quadro sai sem AO
        // N9.6 fase 1: a qualidade (alta: o alvo inteiro, 8 x 4, a primeira versao; media: meia resolucao, 8 x 4; baixa:
        // meia resolucao, 4 x 3). So a imagem da qualidade escolhida existe; a da outra sai ao trocar (o quadro anterior ja
        // terminou: o submit espera a cerca)
        const AoQuality q    = AoQuality(aoQuality_.load(std::memory_order_relaxed));
        const bool      half = q != AoQuality::High;
        Image&          mine = half ? aoHalf_ : aoImg_;
        Image&          other = half ? aoImg_ : aoHalf_;
        if (other.img) freeImage(other), aoSetValid_ = false;
        if (mine.img == VK_NULL_HANDLE) {
            MemScope mc(telemetry::Mem::Targets);
            mine = half ? makeImage((w_ + 1) / 2, (h_ + 1) / 2, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT)
                        : makeImage(w_, h_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            if (!mine.img) {  // sem memoria para a imagem do AO: o quadro sai sem ele, como o desligado
                std::fprintf(stderr, "[render-vk] oclusao ambiente: sem a imagem do AO neste alvo\n");
                return true;
            }
            aoSetValid_ = false;
        }
        // revisao Codex 20261110-161728: o tamanho das imagens do AO vivas (pixels x formato), para os testes e o diagnostico
        st_.ao_image_bytes = (aoImg_.img ? uint64_t(w_) * h_ * 4 : 0) +
                             (aoHalf_.img ? uint64_t((w_ + 1) / 2) * ((h_ + 1) / 2) * 16 : 0) +
                             (aoSurf_.img ? uint64_t(w_) * h_ * 4 : 0);  // N9.6 fase 2b
        if (!aoSetValid_) writeAoSet();
        imageBarrier(mine.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        // so o build de validacao: BS_AO_DIAGNOSTICO=1 a cor da cena vira o AO; =2 preta e =3 branca (as duas mascaras: o
        // pixel igual nas duas nao depende da cena por baixo, pela monotonia da mistura e da quantizacao)
        static const uint32_t debug = [] {
#if BS_VALIDACAO
            const char* v = std::getenv("BS_AO_DIAGNOSTICO");
            return v && *v >= '1' && *v <= '3' ? uint32_t(*v - '0') : 0u;
#else
            return 0u;
#endif
        }();
        const uint32_t dirs = q == AoQuality::Low ? 4u : 8u, steps = q == AoQuality::Low ? 3u : 4u;
        struct {
            uint32_t w, h;
            float    invScale, cx, cy, dist;
            uint32_t raw;
            float    bias, depthScale, radius, strength;
            uint32_t debug, dirs, steps;
            float    angleStep;
            uint32_t pad;
        } pc{w_, h_, 1.f / scale_, float(fr.width) / 2.f, float(fr.height) / 2.f, 500.f, rawDepth_ ? 1u : 0u, prep_.depth_bias,
             prep_.depth_scale, 60.f, 0.8f, debug, dirs, steps,
             // 8 direcoes: a constante da primeira versao (0.78539816), para a alta repetir a conta
             dirs == 8 ? 0.78539816f : 6.2831853f / float(dirs), 0};
        static_assert(sizeof pc == 64);
        f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, aoPipeLayout_, 0, 1, &aoSet_, 0, nullptr);
        f_.vkCmdPushConstants(cmd_, aoPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
        f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, half ? aoHalfPipe_ : aoPipe_);
        if (half) f_.vkCmdDispatch(cmd_, ((w_ + 1) / 2 + 7) / 8, ((h_ + 1) / 2 + 7) / 8, 1);
        else f_.vkCmdDispatch(cmd_, (w_ + 7) / 8, (h_ + 7) / 8, 1);
        imageBarrier(mine.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        // N9.6 fase 2b: com uma translucida do catalogo no quadro, o passe final grava tambem o fator e o 1/w do opaco
        const bool surf = aoSurfWanted_ && aoSurf_.img && aoSurfBound_;
        if (surf)
            imageBarrier(aoSurf_.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE,
                             half ? (surf ? aoUpSurfPipe_ : aoUpPipe_) : (surf ? aoBlurSurfPipe_ : aoBlurPipe_));
        f_.vkCmdDispatch(cmd_, (w_ + 7) / 8, (h_ + 7) / 8, 1);
        if (surf) {  // lida pelas translucidas do passe B (pvr.frag, binding 6)
            imageBarrier(aoSurf_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
            aoSurfFrame_ = true, ++st_.ao_surface_frames;
        }
        // a cor escrita pelo compute: lida e escrita pelos volumes, pelo passe B e pelas copias
        imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        st_.ao_half_frames += half;
        ++st_.ao_frames;
        return true;
    }

    uint32_t downW_ = 0, downH_ = 0;  // tamanho do jogo do alvo atual
    void freeTarget() {
        freeBuffer(down_);
        for (auto& [k, p] : pipes_) f_.vkDestroyPipeline(dev_, p, nullptr), --g_live;
        pipes_.clear();
        if (fb_) f_.vkDestroyFramebuffer(dev_, fb_, nullptr), --g_live, fb_ = VK_NULL_HANDLE;
        if (pass_) f_.vkDestroyRenderPass(dev_, pass_, nullptr), --g_live, pass_ = VK_NULL_HANDLE;
        if (fbB_) f_.vkDestroyFramebuffer(dev_, fbB_, nullptr), --g_live, fbB_ = VK_NULL_HANDLE;
        if (passB_) f_.vkDestroyRenderPass(dev_, passB_, nullptr), --g_live, passB_ = VK_NULL_HANDLE;
        freeImage(color_), freeImage(depth_), freeImage(shadow_), freeImage(waterScene_), freeImage(aoImg_),
            freeImage(aoHalf_), freeImage(aoSurf_);
        freeImage(shadowMask_);
        st_.ao_image_bytes = 0, aoSurfBound_ = false, lightSetValid_ = false, vmSetValid_ = softSetValid_ = false;
        waterSceneReady_ = false, waterBound_ = false, waterFailed_ = false, st_.water_scene_bytes = 0;  // N9.7
        aoSetValid_ = false;
        freeBuffer(heads_), freeBuffer(frags_), freeBuffer(readback_), freeBuffer(headerRead_);
    }

    static VkBlendFactor blendFactor(Blend b, bool src) {
        switch (b) {
        case Blend::Zero: return VK_BLEND_FACTOR_ZERO;
        case Blend::One: return VK_BLEND_FACTOR_ONE;
        case Blend::OtherColor: return src ? VK_BLEND_FACTOR_DST_COLOR : VK_BLEND_FACTOR_SRC_COLOR;
        case Blend::InvOtherColor: return src ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case Blend::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case Blend::InvSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case Blend::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        default: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        }
    }

    // pipeline pela parte fixa do estado: modo, comparacao e escrita de profundidade, fatores de mistura
    // N9.7: water = a variante -DWATER do fragmento (desenho de agua com a agua aprimorada); a parte fixa e a mesma
    VkPipeline pipeline(PassMode mode, const DrawState& s, bool water = false) {
        uint32_t cmp = uint32_t(s.depth), zw = s.depth_write ? 1 : 0, src = 0, dst = 0;
        if (mode == PassMode::PunchThrough) cmp = 6, zw = 1;  // >= forcado; z-write disable ignorado
        if (mode == PassMode::TranslucentAutosort) cmp = 6, zw = 0;
        if (mode == PassMode::TranslucentOrdered) src = uint32_t(s.src), dst = uint32_t(s.dst);
        const uint32_t key = uint32_t(mode) | (cmp << 2) | (zw << 5) | (src << 6) | (dst << 9) | (uint32_t(water) << 12);
        if (auto it = pipes_.find(key); it != pipes_.end()) return it->second;

        VkPipelineShaderStageCreateInfo st[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
             mode == PassMode::TranslucentAutosort ? (water ? waterFragList_ : fragList_) : (water ? waterFrag_ : frag_), "main",
             nullptr}};
        VkVertexInputBindingDescription   vb{0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription va[7] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},  {1, 0, VK_FORMAT_R32_SFLOAT, 8},
                                                   {2, 0, VK_FORMAT_R32G32_SFLOAT, 12}, {3, 0, VK_FORMAT_R32_UINT, 20},
                                                   {4, 0, VK_FORMAT_R32_UINT, 24},      {5, 0, VK_FORMAT_R32_UINT, 28},
                                                   {6, 0, VK_FORMAT_R32_UINT, 32}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1, vi.pVertexBindingDescriptions = &vb;
        vi.vertexAttributeDescriptionCount = 7, vi.pVertexAttributeDescriptions = va;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1, vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        // sem recorte por z (o PVR nao recorta por profundidade): depthClamp; culling ja feito na CPU
        rs.depthClampEnable = VK_TRUE, rs.polygonMode = VK_POLYGON_MODE_FILL, rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE, rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable  = mode == PassMode::TranslucentAutosort ? VK_FALSE : VK_TRUE;  // no autosort: no shader
        ds.depthWriteEnable = zw ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = VkCompareOp(cmp);  // a mesma numeracao do ISP (0 nunca ... 7 sempre)
        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = mode == PassMode::TranslucentAutosort ? 0u : 0xFu;
        if (mode == PassMode::TranslucentOrdered) {
            cb.blendEnable         = VK_TRUE;
            cb.srcColorBlendFactor = cb.srcAlphaBlendFactor = blendFactor(s.src, true);
            cb.dstColorBlendFactor = cb.dstAlphaBlendFactor = blendFactor(s.dst, false);
            cb.colorBlendOp = cb.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        // N8.7: opacas e punch-through no passe A, que tem o anexo do bit de sombra (R8_UINT). O estado e o mesmo nos dois
        // anexos (sem mistura nesse passe; o R8_UINT ignora G, B e A): nao exige o recurso independentBlend
        const bool                          inA = mode == PassMode::Opaque || mode == PassMode::PunchThrough;
        VkPipelineColorBlendAttachmentState cbsAll[2] = {cb, cb};
        VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cbs.attachmentCount = inA ? 2 : 1, cbs.pAttachments = cbsAll;
        VkDynamicState                   dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2, dy.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2, gp.pStages = st, gp.pVertexInputState = &vi, gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp, gp.pRasterizationState = &rs, gp.pMultisampleState = &ms, gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cbs, gp.pDynamicState = &dy, gp.layout = gfxLayout_, gp.renderPass = pass_;
        gp.renderPass = inA ? pass_ : passB_;
        gp.subpass    = mode == PassMode::TranslucentAutosort ? 1 : 0;
        VkPipeline p = VK_NULL_HANDLE;
        if (f_.vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
        ++pipelinesCreated_;
        ++g_live, ++st_.pipelines;
        pipes_[key] = p;
        return p;
    }

    void imageBarrier(VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags ss, VkAccessFlags sa,
                      VkPipelineStageFlags ds, VkAccessFlags da) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from, b.newLayout = to, b.srcAccessMask = sa, b.dstAccessMask = da;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img, b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        f_.vkCmdPipelineBarrier(cmd_, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void bufferBarrier(VkBuffer buf, VkPipelineStageFlags ss, VkAccessFlags sa, VkPipelineStageFlags ds, VkAccessFlags da) {
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        b.srcAccessMask = sa, b.dstAccessMask = da, b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = buf, b.offset = 0, b.size = VK_WHOLE_SIZE;
        f_.vkCmdPipelineBarrier(cmd_, ss, ds, 0, 0, nullptr, 1, &b, 0, nullptr);
    }

    Push push(const Batch& b, const FrameParams& fr, const Tex* tex) const {
        const DrawState& s = b.state;
        Push             p{};
        p.size[0] = float(w_), p.size[1] = float(h_), p.depthScale = prep_.depth_scale;
        uint32_t fl = 0;
        if (tex) {
            fl |= F_TEXTURED;
            p.texSize[0] = tex->desc.width, p.texSize[1] = tex->desc.height;
        }
        if (s.gouraud) fl |= F_GOURAUD;
        if (s.offset_color) fl |= F_OFFSET;
        if (s.use_alpha) fl |= F_USE_ALPHA;
        if (s.ignore_tex_alpha) fl |= F_IGNORE_TEX_ALPHA;
        if (s.color_clamp) fl |= F_CLAMP;
        if (s.clip == Clip::Outside) fl |= F_CLIP_OUTSIDE;
        fl |= uint32_t(s.env) << 8;
        fl |= uint32_t(s.filter == Filter::Point ? 0 : 1) << 10;
        if (s.wrap_u == Wrap::Clamp) fl |= F_CLAMP_U;
        if (s.wrap_v == Wrap::Clamp) fl |= F_CLAMP_V;
        if (s.wrap_u == Wrap::Mirror) fl |= F_FLIP_U;
        if (s.wrap_v == Wrap::Mirror) fl |= F_FLIP_V;
        if (fr.texel_center) fl |= F_TEXEL_HALF;
        fl |= F_EXACT;  // modo fiel: a cobertura do software
        if (s.fog == Fog::Table && !soft::fog_volumes_disabled()) fl |= F_FOG;  // N8.7
        if (s.shadow) fl |= F_SHADOW;
        if (b.scene3d) fl |= F_SCENE;
        // N9.6 fase 2b: uma translucida do catalogo, com o AO escrito para ela neste quadro
        if (aoSurfFrame_ && tex && (tex->material & kMaterialAoSurface) &&
            (b.mode == PassMode::TranslucentOrdered || b.mode == PassMode::TranslucentAutosort))
            fl |= F_AO_SURF;
        if (!fr.sample_center) fl |= F_SAMPLE_CORNER;
        if (!fr.attribute_center) fl |= F_ATTR_CORNER;
        if (rawDepth_) fl |= F_RAW_DEPTH;
        // atributos (TSP) numa amostra diferente da cobertura (FPU): HALF_OFFSET bits 1 e 0
        if (fr.attribute_center && !fr.sample_center) fl |= F_ATTR_PLUS;
        if (!fr.attribute_center && fr.sample_center) fl |= F_ATTR_MINUS;
        fl |= uint32_t(b.mode) << 17;
        p.flags   = fl;
        p.clip[0] = s.clip_x0, p.clip[1] = s.clip_y0, p.clip[2] = s.clip_x1, p.clip[3] = s.clip_y1;
        p.ptRef   = uint32_t(std::lround(fr.punch_through_ref * 255));
        p.blend   = (uint32_t(s.src) << 3) | uint32_t(s.dst);
        p.clampMin = fr.clamp_min, p.clampMax = fr.clamp_max;
        p.depthBias = prep_.depth_bias;
        p.invScale  = 1.f / scale_;
        return p;  // N9.7: water[] so nos desenhos de agua (o laco dos desenhos), que vao pela variante -DWATER
    }

    // N8.7: os volumes modificadores da lista 1 (volumes.comp), entre os passes A e B. So com a sombra por intensidade
    // (FPU_SHAD_SCALE bit 8) e volumes no quadro. A CPU separa os triangulos por tile de 32 x 32 pixels do jogo, na ordem
    // de submissao, sem os descartados (o cull do volume, area nula ou nao finita), com a area e 1/area calculadas como no
    // rasterizador em software. Os volumes da lista 3 (sobre as translucidas) nao sao aplicados (contados).
    bool volumes(const CommandList& cl) {
        const FrameParams& fr = cl.frame;
        // N9.6 fase 4: desligadas, a memoria da mascara volta (os conjuntos dela nao estao vinculados neste quadro)
        if (!shadowsOn_.load(std::memory_order_relaxed) && shadowMask_.img)
            freeImage(shadowMask_), vmSetValid_ = softSetValid_ = false;
        bool               any = false;
        for (const ModifierVolume& m : cl.modifiers) {
            if (m.list == ListType::TranslucentModifier) ++st_.translucent_volumes_ignored;
            else any = true;
        }
        if (!any || !(fr.shadow_scale & 0x100) || soft::fog_volumes_disabled()) return true;
        const uint32_t tilesX = (fr.width + 31) / 32, tilesY = (fr.height + 31) / 32;
        std::vector<uint32_t>              vols;   // 4 por volume
        std::vector<float>                 tris;   // 11 por triangulo
        std::vector<std::vector<uint32_t>> bins(size_t(tilesX) * tilesY);  // pares (volume, triangulo)
        uint32_t                           nt = 0;
        for (const ModifierVolume& m : cl.modifiers) {
            if (m.list != ListType::OpaqueModifier) continue;
            const uint32_t v = uint32_t(vols.size() / 4);
            vols.insert(vols.end(), {nt, m.triangle_count, (m.inclusion ? 1u : 0u) | (m.closed ? 2u : 0u), m.or_from});
            for (uint32_t k = 0; k < m.triangle_count; ++k, ++nt) {
                const Vertex& a = cl.vertices[m.first_vertex + 3 * k];
                const Vertex& b = cl.vertices[m.first_vertex + 3 * k + 1];
                const Vertex& c = cl.vertices[m.first_vertex + 3 * k + 2];
                const float   area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                const float   inv  = 1.f / area;
                tris.insert(tris.end(), {a.x, a.y, b.x, b.y, c.x, c.y, a.inv_w, b.inv_w, c.inv_w, area, inv});
                if (area == 0 || !std::isfinite(area)) continue;
                if (m.cull == 1 && std::fabs(area) < 2 * fr.small_cull) continue;
                if (m.cull == 2 && area < 0) continue;
                if (m.cull == 3 && area > 0) continue;
                const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
                const int x1 = std::min(int(fr.width) - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
                const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
                const int y1 = std::min(int(fr.height) - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
                if (x0 > x1 || y0 > y1) continue;
                for (int ty = y0 / 32; ty <= y1 / 32; ++ty)
                    for (int tx = x0 / 32; tx <= x1 / 32; ++tx) bins[size_t(ty) * tilesX + tx].insert(bins[size_t(ty) * tilesX + tx].end(), {v, nt});
            }
        }
        std::vector<uint32_t> tiles(bins.size() + 1, 0), ent;
        for (size_t t = 0; t < bins.size(); ++t) {
            tiles[t] = uint32_t(ent.size() / 2);
            ent.insert(ent.end(), bins[t].begin(), bins[t].end());
        }
        tiles[bins.size()] = uint32_t(ent.size() / 2);
        if (ent.empty()) ent = {0, 0};  // buffers nao vazios
        if (tris.empty()) tris.assign(11, 0.f);
        // os buffers crescem; cada realocacao regrava o conjunto (o handle novo pode repetir o liberado)
        bool rewrite = !volSetValid_;
        auto put = [&](Buffer& buf, const void* data, size_t bytes) {
            if (buf.size < bytes) {
                if (!ensureHost(buf, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
                rewrite = true;
            }
            std::memcpy(buf.map, data, bytes);
            return true;
        };
        if (!put(volVols_, vols.data(), vols.size() * 4) || !put(volTris_, tris.data(), tris.size() * 4) ||
            !put(volTiles_, tiles.data(), tiles.size() * 4) || !put(volEnt_, ent.data(), ent.size() * 4))
            return false;
        if (rewrite) writeVolSet();
        // N9.6 fase 4: com as sombras aprimoradas (a Remastered, o render do framebuffer), a mascara e a borda suave
        bool soft = shadowsOn_.load(std::memory_order_relaxed) && fr.height == 480 && fr.width >= 640 && softObjects();
        if (soft && !shadowMask_.img) {
            MemScope mc(telemetry::Mem::Targets);
            shadowMask_ = makeImage(w_, h_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            vmSetValid_ = softSetValid_ = false;
            if (!shadowMask_.img) soft = false;  // sem memoria: o caminho fiel
        }
        if (soft) {
            if (rewrite || !vmSetValid_ || !softSetValid_) writeSoftSets();
            imageBarrier(shadowMask_.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        }
        f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, soft ? vmPipe_ : volPipe_);
        f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, soft ? vmPipeLayout_ : volPipeLayout_, 0, 1,
                                   soft ? &vmSet_ : &volSet_, 0, nullptr);
        struct {
            uint32_t w, h, tilesX, scale;
            float    invScale, fpuOff;
            uint32_t raw;
            float    bias, depthScale;
        } pc{w_, h_, tilesX, fr.shadow_scale & 0xFF, 1.f / scale_, fr.sample_center ? 0.5f : 0.f, rawDepth_ ? 1u : 0u,
             prep_.depth_bias, prep_.depth_scale};
        static_assert(sizeof pc == 36);
        f_.vkCmdPushConstants(cmd_, soft ? vmPipeLayout_ : volPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
        f_.vkCmdDispatch(cmd_, (w_ + 7) / 8, (h_ + 7) / 8, 1);
        if (soft) {  // a borda suave: a mascara media da vizinhanca escolhe a escala (o interior fica com a conta do original)
            imageBarrier(shadowMask_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
            const int radius = std::max(1, int(std::lround(2.0f * scale_)));  // 2 pixels do jogo
            struct {
                uint32_t w, h, scale;
                int32_t  radius, stride;
                uint32_t raw;
                float    bias, depthScale;
            } sp{w_, h_, fr.shadow_scale & 0xFF, radius, std::max(1, radius / 4), rawDepth_ ? 1u : 0u, prep_.depth_bias,
                 prep_.depth_scale};
            static_assert(sizeof sp == 32);
            f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, softPipe_);
            f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, softPipeLayout_, 0, 1, &softSet_, 0, nullptr);
            f_.vkCmdPushConstants(cmd_, softPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof sp, &sp);
            f_.vkCmdDispatch(cmd_, (w_ + 7) / 8, (h_ + 7) / 8, 1);
            ++st_.soft_shadow_frames;
        }
        // a cor escrita pelo compute: lida e escrita pelo passe B e pelas copias
        imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        ++st_.volume_frames, st_.volume_triangles += nt;
        return true;
    }

    Result<void> record(const CommandList& cl) {
        const FrameParams& fr = cl.frame;
        waterFramesRec_ = waterDrawsRec_ = 0;  // N9.7: os da tentativa (publicados no fim do submitFrame)
        const size_t       vbytes = std::max<size_t>(prep_.vertices.size() * sizeof(GpuVertex), 64);
        if (!ensureHost(verts_, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return fail("buffer de vertices");
        if (!prep_.vertices.empty()) std::memcpy(verts_.map, prep_.vertices.data(), prep_.vertices.size() * sizeof(GpuVertex));
        const size_t tbytes = std::max<size_t>(prep_.tri_data.size() * sizeof(uint32_t), 64);
        if (tris_.size < tbytes) {
            // o buffer novo pode receber o MESMO handle do que acabou de ser liberado (o driver reaproveita): o descritor
            // e sempre regravado. Comparar com o handle antigo deixava o descritor na memoria liberada e o shader lia uma
            // pagina sem mapeamento (VK_ERROR_DEVICE_LOST com "Invalid Read" na AMD integrada; docs/N8-7-DIAGNOSTICO.md)
            if (!ensureHost(tris_, tbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return fail("dados dos triangulos");
            ++st_.tri_reallocs;
            if (std::getenv("BS_VK_ENVIOS"))
                std::fprintf(stderr, "[render-vk] triangulos realocados no render %llu: %zu bytes\n",
                             static_cast<unsigned long long>(st_.frames + 1), size_t(tris_.size));
            writeFrameSets();
        }
        if (!prep_.tri_data.empty()) std::memcpy(tris_.map, prep_.tri_data.data(), prep_.tri_data.size() * sizeof(uint32_t));
        {  // N8.7: FrameData (pvr.frag): a tabela da fog, a cor e a densidade
            uint32_t fd[kFrameDataBytes / 4] = {};
            for (size_t i = 0; i < fr.fog_table.size(); ++i) fd[i / 2] |= uint32_t(fr.fog_table[i]) << ((i & 1) * 16);
            fd[64] = fr.fog_color & 0xFFFFFF, fd[65] = fr.fog_density & 0xFFFF;
            std::memcpy(frameData_.map, fd, sizeof fd);
        }
        if (!uploadBytes_.empty() && !ensureHost(staging_, uploadBytes_.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
            return Error::Io;
        if (!uploadBytes_.empty()) std::memcpy(staging_.map, uploadBytes_.data(), uploadBytes_.size());

        // N9.6 fase 2b: o AO para as translucidas do catalogo de materiais. A imagem nasce no primeiro quadro com uma delas
        // (com o AO ligado no render do framebuffer) e sai com o AO desligado; o descritor do quadro e regravado aqui, antes
        // de os comandos o usarem (o quadro anterior ja terminou: o submit espera a cerca)
        aoSurfFrame_  = false;
        aoSurfWanted_ = false;
        if (aoOn_.load(std::memory_order_relaxed) && fr.height == 480 && fr.width >= 640 && !aoFailed_)
            for (const Batch& b : prep_.batches)
                if ((b.mode == PassMode::TranslucentOrdered || b.mode == PassMode::TranslucentAutosort) && b.state.texture) {
                    auto it = texs_.find(b.state.texture.value);
                    if (it != texs_.end() && (it->second.material & kMaterialAoSurface)) {
                        aoSurfWanted_ = true;
                        break;
                    }
                }
        if (aoSurfWanted_ && !aoSurf_.img) {
            MemScope mc(telemetry::Mem::Targets);
            aoSurf_ = makeImage(w_, h_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT);
            aoSetValid_ = false;
        }
        if (!aoOn_.load(std::memory_order_relaxed) && aoSurf_.img) freeImage(aoSurf_), aoSetValid_ = false;
        // N9.7: a imagem da cena para a agua nasce no primeiro quadro com um desenho de agua (agua aprimorada ligada) e sai
        // com a agua desligada; nunca na Original (o catalogo nao chega la) nem com a agua original. O descritor e regravado
        // aqui, antes de os comandos o usarem
        if (waterWanted_ && !waterScene_.img) {
            MemScope mc(telemetry::Mem::Targets);
            waterScene_ = makeImage(w_, h_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
            waterSceneReady_ = false;
            if (!waterScene_.img) {  // sem memoria: a agua fica original ate o alvo mudar
                std::fprintf(stderr, "[render-vk] agua: sem a imagem da cena neste alvo (a agua fica original)\n");
                waterFailed_ = true, waterWanted_ = false;
            }
        }
        if (waterQuality_ == WaterQuality::Original && waterScene_.img) freeImage(waterScene_), waterSceneReady_ = false;
        st_.water_scene_bytes = waterScene_.img ? uint64_t(waterScene_.alloc) : 0;  // o alocado (revisao Codex 20261111-121354)
        if (bool(aoSurf_.img) != aoSurfBound_ || bool(waterScene_.img) != waterBound_) writeFrameSets();
        gatherLights(cl);  // N9.6 fase 3

        f_.vkResetCommandBuffer(cmd_, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        f_.vkBeginCommandBuffer(cmd_, &bi);
        // N9.5 fase 5C (BS_TELEMETRIA): os tempos de GPU por etapa, lidos depois da cerca que o render ja espera
        const bool ts = timing();
        if (ts) f_.vkCmdResetQueryPool(cmd_, tsPool_, 0, 5), f_.vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tsPool_, 0);
        // envios de textura pendentes
        // diagnostico BS_VK_ENVIOS=1: cada envio (render, textura, tamanho, nova ou atualizada, deslocamento na area de envio)
        static const bool envios = std::getenv("BS_VK_ENVIOS") != nullptr;
        for (const Upload& u : uploads_) {
            Tex* t = u.tex == dummyId_ ? &dummy_ : &texs_[u.tex];
            if (envios)
                std::fprintf(stderr, "[render-vk] envio render %llu: textura %u %ux%u %s, deslocamento %zu de %zu\n",
                             static_cast<unsigned long long>(st_.frames + 1), u.tex, t->desc.width, t->desc.height,
                             t->ready ? "atualizada" : "nova", u.offset, uploadBytes_.size());
            imageBarrier(t->image.img, t->ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkBufferImageCopy c{};
            c.bufferOffset = u.offset, c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.imageExtent = {t->desc.width, t->desc.height, 1};
            f_.vkCmdCopyBufferToImage(cmd_, staging_.buf, t->image.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            imageBarrier(t->image.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_SHADER_READ_BIT);
            t->ready = true;
        }
        // listas por pixel vazias: contador 0, capacidade, transbordo 0, largura; cabecas = nenhuma
        const uint32_t hdr[8] = {0, fragCapacity_, 0, w_, 0, 0, 0, 0};  // contador, capacidade, transbordo, largura, maior lista
        f_.vkCmdUpdateBuffer(cmd_, heads_.buf, 0, kHeader, hdr);
        f_.vkCmdFillBuffer(cmd_, heads_.buf, kHeader, VkDeviceSize(w_) * h_ * 4, kNone);
        bufferBarrier(heads_.buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        VkClearValue clear[3] = {};
        const uint32_t c = fr.clear_color;
        clear[0].color = {{float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float(c & 0xFF) / 255.f,
                           float(c >> 24) / 255.f}};
        const float cz = !std::isfinite(fr.clear_depth) ? 0.f
                         : rawDepth_ ? fr.clear_depth
                                     : std::clamp((fr.clear_depth - prep_.depth_bias) * prep_.depth_scale, 0.f, 1.f);
        clear[1].depthStencil = {cz, 0};
        clear[2].color.uint32[0] = 0;  // N8.7: o bit de sombra
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = pass_, rb.framebuffer = fb_, rb.renderArea = {{0, 0}, {w_, h_}}, rb.clearValueCount = 3, rb.pClearValues = clear;
        if (ts) f_.vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool_, 1);  // os envios terminaram
        f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
        // com a profundidade crua, o clamp do depthClamp vai a +-1e30 (o 1/w do jogo cabe; o shader escreve gl_FragDepth)
        VkViewport vp{0, 0, float(w_), float(h_), rawDepth_ ? -1e30f : 0.f, rawDepth_ ? 1e30f : 1.f};
        f_.vkCmdSetViewport(cmd_, 0, 1, &vp);
        const VkDeviceSize zero = 0;
        f_.vkCmdBindVertexBuffers(cmd_, 0, 1, &verts_.buf, &zero);
        f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxLayout_, 1, 1, &fragSet_, 0, nullptr);
        VkPipeline bound = VK_NULL_HANDLE;
        bool       inB = false, second = false;
        // N9.7: a agua aprimorada so com a imagem da cena criada para este quadro (waterWanted_: um desenho de agua)
        const bool waterFrame = waterWanted_ && waterScene_.img != VK_NULL_HANDLE;
        bool       copiedWaterScene = false;
        // N8.7: o fim do passe A (opacas e punch-through), os volumes e o comeco do passe B (translucidas)
        auto toB = [&]() -> bool {
            f_.vkCmdEndRenderPass(cmd_);
            if (!ambientOcclusion(cl) || !lighting(cl) || !volumes(cl)) return false;
            if (waterFrame) {
                // N9.7: fotografia da cena opaca depois dos volumes. A agua, desenhada no passe B, pode entao refletir
                // e refratar o ambiente sem realimentar o proprio anexo de cor.
                imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                imageBarrier(waterScene_.img,
                             waterSceneReady_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             waterSceneReady_ ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             waterSceneReady_ ? VK_ACCESS_SHADER_READ_BIT : 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT);
                VkImageCopy wc{};
                wc.srcSubresource = wc.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                wc.extent = {w_, h_, 1};
                f_.vkCmdCopyImage(cmd_, color_.img, VK_IMAGE_LAYOUT_GENERAL, waterScene_.img,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &wc);
                imageBarrier(waterScene_.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_SHADER_READ_BIT);
                imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
                copiedWaterScene = true, ++waterFramesRec_;
            }
            VkRenderPassBeginInfo rbB{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rbB.renderPass = passB_, rbB.framebuffer = fbB_, rbB.renderArea = {{0, 0}, {w_, h_}};
            f_.vkCmdBeginRenderPass(cmd_, &rbB, VK_SUBPASS_CONTENTS_INLINE);
            inB = true, bound = VK_NULL_HANDLE;
            return true;
        };
        for (const Batch& b : prep_.batches) {
            if (!inB && b.mode != PassMode::Opaque && b.mode != PassMode::PunchThrough && !toB()) return fail("AO ou volumes");
            if (b.mode == PassMode::TranslucentAutosort && !second) {
                f_.vkCmdNextSubpass(cmd_, VK_SUBPASS_CONTENTS_INLINE), second = true, bound = VK_NULL_HANDLE;
            }
            const Tex* tex = nullptr;
            if (b.state.texture) {
                auto it = texs_.find(b.state.texture.value);
                if (it != texs_.end() && it->second.ready) tex = &it->second;
            }
            // N9.7: um desenho de agua (translucido de uma textura com kMaterialWater) vai pela variante -DWATER
            const bool water = waterFrame && tex && (tex->material & kMaterialWater) &&
                               (b.mode == PassMode::TranslucentOrdered || b.mode == PassMode::TranslucentAutosort);
            VkPipeline p = pipeline(b.mode, b.state, water);
            if (!p) return fail("pipeline");  // o buffer de comandos e recomecado (reset) no proximo quadro
            if (p != bound) f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, p), bound = p;
            VkRect2D sc{{0, 0}, {w_, h_}};
            if (b.scissor) {  // o recorte "dentro" em pixels do jogo -> os pixels do alvo que ele cobre
                auto      lo = [&](int v) { return scale_ == 1.f ? v : int(std::floor(float(v) * scale_)); };
                auto      hi = [&](int v) { return scale_ == 1.f ? v : int(std::ceil(float(v + 1) * scale_)) - 1; };
                const int x0 = std::max(0, lo(b.state.clip_x0)), y0 = std::max(0, lo(b.state.clip_y0));
                const int x1 = std::min(int(w_) - 1, hi(b.state.clip_x1)), y1 = std::min(int(h_) - 1, hi(b.state.clip_y1));
                if (x0 > x1 || y0 > y1) continue;
                sc = {{x0, y0}, {uint32_t(x1 - x0 + 1), uint32_t(y1 - y0 + 1)}};
            }
            f_.vkCmdSetScissor(cmd_, 0, 1, &sc);
            const VkDescriptorSet set = tex ? tex->set : dummy_.set;
            f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxLayout_, 0, 1, &set, 0, nullptr);
            Push pc = push(b, fr, tex);
            if (water) {
                // so o build de validacao: BS_AGUA_DIAGNOSTICO=1 pinta os desenhos de agua de magenta, =2 de verde (as duas
                // mascaras do dominio da agua, para a suite n97_agua); =3 e =4 o mesmo sem o movimento da textura (a pegada
                // da agua parada: o dominio e a uniao das duas)
                static const uint32_t waterDebug = [] {
#if BS_VALIDACAO
                    const char* v = std::getenv("BS_AGUA_DIAGNOSTICO");
                    return v && *v >= '1' && *v <= '4' ? uint32_t(*v - '0') : 0u;
#else
                    return 0u;
#endif
                }();
                pc.water[0] = waterDebug ? float(2 + waterDebug) : float(waterQuality_), pc.water[1] = waterTime_;
                pc.water[2] = waterHorizon_ * scale_, pc.water[3] = waterBottom_ * scale_;
                ++waterDrawsRec_;
            }
            // o fiel le os 64 primeiros bytes (o bloco do N8.7); a agua, os 80
            f_.vkCmdPushConstants(cmd_, gfxLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                  water ? uint32_t(sizeof pc) : uint32_t(offsetof(Push, water)), &pc);
            f_.vkCmdDraw(cmd_, b.count, 1, b.first, 0);
        }
        if (!inB) {  // sem translucidas: so o passe A (e os volumes)
            f_.vkCmdEndRenderPass(cmd_);
            if (!ambientOcclusion(cl) || !lighting(cl) || !volumes(cl)) return fail("AO, luz ou volumes");
        } else {
            if (!second) f_.vkCmdNextSubpass(cmd_, VK_SUBPASS_CONTENTS_INLINE);
            f_.vkCmdEndRenderPass(cmd_);  // cor em GENERAL (dependencia externa: compute e transferencia)
        }
        if (ts) f_.vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool_, 2);  // passes A e B e os volumes
        if (prep_.has_autosort) {
            f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, resolve_);
            f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, compLayout_, 0, 1, &resolveSet_, 0, nullptr);
            const uint32_t sz[2] = {w_, h_};
            f_.vkCmdPushConstants(cmd_, compLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, sz);
            f_.vkCmdDispatch(cmd_, (w_ + 7) / 8, (h_ + 7) / 8, 1);
            imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        }
        if (ts) f_.vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool_, 3);  // o resolve
        bufferBarrier(heads_.buf, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
        // N7c: a imagem do alvo vai para o slot da apresentacao (a escrita do color_ ja terminou: barreira de quem chama)
        auto copySlot = [&] {
            if (writeSlot_ < 0) return;
            Slot& sl = slots_[writeSlot_];
            // GENERAL sempre (amostrado pela apresentacao e copiado pelas capturas sem transicoes); a leitura
            // anterior terminou (a fixacao so cai depois da cerca da apresentacao)
            imageBarrier(sl.image.img, sl.shaderRead ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageCopy ic{};
            ic.srcSubresource = ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, ic.extent = {w_, h_, 1};
            f_.vkCmdCopyImage(cmd_, color_.img, VK_IMAGE_LAYOUT_GENERAL, sl.image.img, VK_IMAGE_LAYOUT_GENERAL, 1, &ic);
            imageBarrier(sl.image.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
            sl.shaderRead = true;
        };
        if (w_ == downW_ && h_ == downH_) {  // resolucao do jogo: a imagem inteira volta (o modo fiel)
            VkBufferImageCopy rc{};
            rc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, rc.imageExtent = {w_, h_, 1};
            f_.vkCmdCopyImageToBuffer(cmd_, color_.img, VK_IMAGE_LAYOUT_GENERAL, readback_.buf, 1, &rc);
            copySlot();  // N9.5 fase 1 (set_always_slot): o quadro largo tambem na escala 1; as duas copias so leem
        } else {
            // N7c: a imagem maior fica na GPU (slot, para a apresentacao) e so a reducao volta para a CPU
            imageBarrier(color_.img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
            copySlot();
            f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, downPipe_);
            f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, downPipeLayout_, 0, 1, &downSet_, 0, nullptr);
            const uint32_t sizes[4] = {w_, h_, downW_, downH_};
            f_.vkCmdPushConstants(cmd_, downPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, sizes);
            f_.vkCmdDispatch(cmd_, (downW_ + 7) / 8, (downH_ + 7) / 8, 1);
            bufferBarrier(down_.buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferCopy dc{0, 0, VkDeviceSize(downW_) * downH_ * 4};
            f_.vkCmdCopyBuffer(cmd_, down_.buf, readback_.buf, 1, &dc);
        }
        st_.readback_bytes += uint64_t(downW_) * downH_ * 4;
        // a maior lista e escrita pelo resolve (compute): a copia espera por ele (barreira de compute para transferencia)
        bufferBarrier(heads_.buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferCopy hc{0, 0, kHeader};
        f_.vkCmdCopyBuffer(cmd_, heads_.buf, headerRead_.buf, 1, &hc);
        VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
        if (ts) f_.vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool_, 4);  // a saida (slot, reducao, leitura)
        f_.vkEndCommandBuffer(cmd_);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1, si.pCommandBuffers = &cmd_;
        if (writeSlot_ >= 0 && presentQueue_ != queue_ && slots_[writeSlot_].ready) {
            // a apresentacao (outra fila) espera a escrita do slot por este semaforo
            si.signalSemaphoreCount = 1, si.pSignalSemaphores = &slots_[writeSlot_].ready;
        }
        f_.vkResetFences(dev_, 1, &fence_);
        // diagnostico: BS_VK_ATRASO_MS=<ms> espera antes de cada submissao (a GPU ociosa entre quadros, como no modo
        // comparar, em que o software ocupa a CPU)
        static const int idleMs = [] {
            const char* v = std::getenv("BS_VK_ATRASO_MS");
            return v ? std::atoi(v) : 0;
        }();
        if (idleMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(idleMs));
        const auto t0 = std::chrono::steady_clock::now();
        namespace dg = bs::native::diag;  // BS_TRAVAS (N8.8, H9): desligado, so a leitura de um bool
        dg::set_render(st_.frames);
        // dentro da trava so os instantes (mark); o registro (between) depois de solta-la (revisao Codex 2026-11-01)
        VkResult       r  = VK_SUCCESS;
        const uint64_t a0 = dg::start();
        uint64_t       a1 = 0, a2 = 0;
        {  // a fila e compartilhada com a apresentacao (thread da janela)
            std::lock_guard<std::mutex> lock(queueMutex_);
            a1 = dg::mark(a0);
            r  = f_.vkQueueSubmit(queue_, 1, &si, fence_);
            a2 = dg::mark(a0);
            if (r != VK_SUCCESS) return fail("vkQueueSubmit", r);  // como antes: com a trava na mao
        }
        dg::between(dg::Stall::GameQueueLock, a0, a1), dg::between(dg::Stall::GameSubmit, a1, a2);
        {
            const uint64_t tf = dg::start();
            r                 = f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
            dg::finish(dg::Stall::GameFence, tf);
            if (r != VK_SUCCESS) return fail("vkWaitForFences", r);
        }
        // a perda do dispositivo pode sinalizar a cerca sem o trabalho feito (AMD integrada, 2026-10-22: o quadro saia
        // com a imagem anterior e so o envio seguinte acusava): a fila ociosa confirma que o quadro terminou de fato
        {
            const uint64_t b0 = dg::start();
            uint64_t       b1 = 0, b2 = 0;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                b1 = dg::mark(b0);
                r  = f_.vkQueueWaitIdle(queue_);
                b2 = dg::mark(b0);
                if (r != VK_SUCCESS) return fail("vkQueueWaitIdle", r);  // como antes: com a trava na mao
            }
            dg::between(dg::Stall::GameQueueLock, b0, b1), dg::between(dg::Stall::GameWaitIdle, b1, b2);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        st_.gpu_ms_total += ms, st_.gpu_ms_max = std::max(st_.gpu_ms_max, ms);
        if (copiedWaterScene) waterSceneReady_ = true;
        if (ts) {  // N9.5 fase 5C: o render ja terminou (cerca e fila ociosa): a leitura nao espera nada
            uint64_t q[5] = {};
            if (f_.vkGetQueryPoolResults(dev_, tsPool_, 0, 5, sizeof q, q, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                auto d = [&](int a, int b) { return double(timestamp_delta(q[a], q[b], tsBits_)) * double(tsPeriod_) / 1e6; };
                telemetry::gpu_frame({d(0, 1), d(1, 2), d(2, 3), d(3, 4)});
            }
        }
        if (budgetExt_ && telemetry::enabled() && st_.frames % 60 == 0) queryBudget();
        uploads_.clear();  // ja copiados (uma nova tentativa do quadro nao os repete)
        return {};
    }

    // N9.5 fase 5C: o conjunto de consultas de timestamp (5 por render), criado so com a telemetria e timestamps na fila
    bool timing() {
        if (!telemetry::enabled() || tsPeriod_ <= 0 || !f_.vkCreateQueryPool || !f_.vkCmdWriteTimestamp) return false;
        if (!tsPool_) {
            VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP, qi.queryCount = 5;
            if (f_.vkCreateQueryPool(dev_, &qi, nullptr, &tsPool_) != VK_SUCCESS) return tsPool_ = VK_NULL_HANDLE, false;
            ++g_live;
        }
        return true;
    }
    // N9.5 fase 5C: o uso e o orcamento da memoria local (VK_EXT_memory_budget), a soma dos heaps locais
    void queryBudget() {
        if (!f_.vkGetPhysicalDeviceMemoryProperties2) return;
        VkPhysicalDeviceMemoryBudgetPropertiesEXT b{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2         p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        p.pNext = &b;
        f_.vkGetPhysicalDeviceMemoryProperties2(phys_, &p);
        int64_t usage = 0, budget = 0;
        for (uint32_t i = 0; i < p.memoryProperties.memoryHeapCount; ++i)
            if (p.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                usage += int64_t(b.heapUsage[i]), budget += int64_t(b.heapBudget[i]);
        telemetry::set_budget(usage, budget);
    }

    void destroy() {
        if (tsPool_) f_.vkDestroyQueryPool(dev_, tsPool_, nullptr), tsPool_ = VK_NULL_HANDLE, --g_live;
        presenter_.reset();  // a apresentacao primeiro (usa o dispositivo e a cadeia)
        post_.reset();
        if (dev_) {
            f_.vkDeviceWaitIdle(dev_);
            for (Slot& sl : slots_) {
                freeImage(sl.image);
                if (sl.ready) f_.vkDestroySemaphore(dev_, sl.ready, nullptr), --g_live, sl.ready = VK_NULL_HANDLE;
            }
            if (downPipe_) f_.vkDestroyPipeline(dev_, downPipe_, nullptr), --g_live, downPipe_ = VK_NULL_HANDLE;
            if (downMod_) f_.vkDestroyShaderModule(dev_, downMod_, nullptr), --g_live, downMod_ = VK_NULL_HANDLE;
            if (downPipeLayout_) f_.vkDestroyPipelineLayout(dev_, downPipeLayout_, nullptr), --g_live, downPipeLayout_ = VK_NULL_HANDLE;
            if (downLayout_) f_.vkDestroyDescriptorSetLayout(dev_, downLayout_, nullptr), --g_live, downLayout_ = VK_NULL_HANDLE;
        }
        if (dev_) {
            f_.vkDeviceWaitIdle(dev_);
            freeTarget();
            for (auto& [id, t] : texs_) freeImage(t.image);
            texs_.clear();
            freeImage(dummy_.image);
            freeBuffer(verts_), freeBuffer(staging_), freeBuffer(tris_), freeBuffer(frameData_);
            freeBuffer(volVols_), freeBuffer(volTris_), freeBuffer(volTiles_), freeBuffer(volEnt_);
            if (volPipe_) f_.vkDestroyPipeline(dev_, volPipe_, nullptr), --g_live, volPipe_ = VK_NULL_HANDLE;
            if (volPipeLayout_) f_.vkDestroyPipelineLayout(dev_, volPipeLayout_, nullptr), --g_live, volPipeLayout_ = VK_NULL_HANDLE;
            if (volPool_) f_.vkDestroyDescriptorPool(dev_, volPool_, nullptr), --g_live, volPool_ = VK_NULL_HANDLE;
            if (volLayout_) f_.vkDestroyDescriptorSetLayout(dev_, volLayout_, nullptr), --g_live, volLayout_ = VK_NULL_HANDLE;
            if (volMod_) f_.vkDestroyShaderModule(dev_, volMod_, nullptr), --g_live, volMod_ = VK_NULL_HANDLE;
            for (VkPipeline* p : {&aoPipe_, &aoBlurPipe_, &aoHalfPipe_, &aoUpPipe_, &aoBlurSurfPipe_, &aoUpSurfPipe_})  // N9.6
                if (*p) f_.vkDestroyPipeline(dev_, *p, nullptr), --g_live, *p = VK_NULL_HANDLE;
            if (aoPipeLayout_) f_.vkDestroyPipelineLayout(dev_, aoPipeLayout_, nullptr), --g_live, aoPipeLayout_ = VK_NULL_HANDLE;
            if (aoPool_) f_.vkDestroyDescriptorPool(dev_, aoPool_, nullptr), --g_live, aoPool_ = VK_NULL_HANDLE;
            if (aoLayout_) f_.vkDestroyDescriptorSetLayout(dev_, aoLayout_, nullptr), --g_live, aoLayout_ = VK_NULL_HANDLE;
            // N9.6 fase 4: as sombras aprimoradas
            for (VkPipeline* p : {&vmPipe_, &softPipe_})
                if (*p) f_.vkDestroyPipeline(dev_, *p, nullptr), --g_live, *p = VK_NULL_HANDLE;
            for (VkPipelineLayout* l : {&vmPipeLayout_, &softPipeLayout_})
                if (*l) f_.vkDestroyPipelineLayout(dev_, *l, nullptr), --g_live, *l = VK_NULL_HANDLE;
            if (vmPool_) f_.vkDestroyDescriptorPool(dev_, vmPool_, nullptr), --g_live, vmPool_ = VK_NULL_HANDLE;
            for (VkDescriptorSetLayout* l : {&vmLayout_, &softLayout_})
                if (*l) f_.vkDestroyDescriptorSetLayout(dev_, *l, nullptr), --g_live, *l = VK_NULL_HANDLE;
            for (VkShaderModule* m : {&vmMod_, &softMod_})
                if (*m) f_.vkDestroyShaderModule(dev_, *m, nullptr), --g_live, *m = VK_NULL_HANDLE;
            // N9.6 fase 3: a luz
            if (lightPipe_) f_.vkDestroyPipeline(dev_, lightPipe_, nullptr), --g_live, lightPipe_ = VK_NULL_HANDLE;
            if (lightPipeLayout_) f_.vkDestroyPipelineLayout(dev_, lightPipeLayout_, nullptr), --g_live, lightPipeLayout_ = VK_NULL_HANDLE;
            if (lightPool_) f_.vkDestroyDescriptorPool(dev_, lightPool_, nullptr), --g_live, lightPool_ = VK_NULL_HANDLE;
            if (lightLayout_) f_.vkDestroyDescriptorSetLayout(dev_, lightLayout_, nullptr), --g_live, lightLayout_ = VK_NULL_HANDLE;
            if (lightMod_) f_.vkDestroyShaderModule(dev_, lightMod_, nullptr), --g_live, lightMod_ = VK_NULL_HANDLE;
            freeBuffer(lightBuf_);
            for (VkShaderModule* m : {&aoMod_, &aoBlurMod_, &aoHalfMod_, &aoUpMod_, &aoBlurSurfMod_, &aoUpSurfMod_})
                if (*m) f_.vkDestroyShaderModule(dev_, *m, nullptr), --g_live, *m = VK_NULL_HANDLE;
            if (resolve_) f_.vkDestroyPipeline(dev_, resolve_, nullptr), --g_live;
            for (VkShaderModule m : {vert_, frag_, fragList_, comp_, waterFrag_, waterFragList_})
                if (m) f_.vkDestroyShaderModule(dev_, m, nullptr), --g_live;
            if (sampler_) f_.vkDestroySampler(dev_, sampler_, nullptr), --g_live;
            if (waterSampler_) f_.vkDestroySampler(dev_, waterSampler_, nullptr), --g_live;
            if (gfxLayout_) f_.vkDestroyPipelineLayout(dev_, gfxLayout_, nullptr), --g_live;
            if (compLayout_) f_.vkDestroyPipelineLayout(dev_, compLayout_, nullptr), --g_live;
            if (texPool_) f_.vkDestroyDescriptorPool(dev_, texPool_, nullptr), --g_live;
            if (framePool_) f_.vkDestroyDescriptorPool(dev_, framePool_, nullptr), --g_live;
            for (VkDescriptorSetLayout l : {texLayout_, fragLayout_, resolveLayout_})
                if (l) f_.vkDestroyDescriptorSetLayout(dev_, l, nullptr), --g_live;
            if (fence_) f_.vkDestroyFence(dev_, fence_, nullptr), --g_live;
            if (pool_) f_.vkDestroyCommandPool(dev_, pool_, nullptr), --g_live;
            f_.vkDestroyDevice(dev_, nullptr), --g_live;
            dev_ = VK_NULL_HANDLE;
        }
        if (inst_) f_.vkDestroyInstance(inst_, nullptr), --g_live, inst_ = VK_NULL_HANDLE;
#ifdef _WIN32
        if (lib_) FreeLibrary(static_cast<HMODULE>(lib_));
#else
        if (lib_) dlclose(lib_);
#endif
        lib_ = nullptr;
    }
};

} // namespace

std::unique_ptr<VulkanDevice> VulkanDevice::create(std::string* error, const std::vector<std::string>& instance_extensions) {
    auto d = std::make_unique<VkDev>();
    if (!d->init(error, instance_extensions)) return nullptr;
    return d;
}

namespace {
struct Shared {
    std::mutex                    m;
    std::vector<std::string>      ext;
    std::weak_ptr<VulkanDevice>   dev;
    std::shared_ptr<VulkanDevice> keep;  // ate alguem perder o dispositivo, ele fica vivo (os dois lados o reusam)
    uint64_t                      generation = 0;
    std::string                   error;
    bool                          failed = false;
};
Shared& shared() {
    static Shared s;
    return s;
}
} // namespace

void vulkan_shared_configure(std::vector<std::string> instance_extensions) {
    std::lock_guard<std::mutex> lock(shared().m);
    shared().ext = std::move(instance_extensions);
}

std::shared_ptr<VulkanDevice> vulkan_shared(std::string* error) {
    Shared&                     s = shared();
    std::lock_guard<std::mutex> lock(s.m);
    if (auto d = s.dev.lock()) return d;
    if (s.failed) {  // sem Vulkan: nao tenta de novo a cada chamada
        if (error) *error = s.error;
        return nullptr;
    }
    std::string err;
    std::shared_ptr<VulkanDevice> d(VulkanDevice::create(&err, s.ext).release());
    if (!d) {
        s.failed = true, s.error = err;
        if (error) *error = err;
        return nullptr;
    }
    s.dev = d, s.keep = d, ++s.generation;
    return d;
}

void vulkan_shared_lost(const VulkanDevice* lost) {
    Shared&                     s = shared();
    std::lock_guard<std::mutex> lock(s.m);
    if (s.keep.get() == lost) s.keep.reset();
    if (auto d = s.dev.lock(); d.get() == lost) s.dev.reset();
    s.failed = false;
}

uint64_t vulkan_shared_generation() {
    std::lock_guard<std::mutex> lock(shared().m);
    return shared().generation;
}

int vulkan_live_objects() { return detail::g_live.load(); }

} // namespace bs::native::render
