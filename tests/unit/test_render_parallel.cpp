// N9-L: as linhas do rasterizador em paralelo: cada uma exatamente uma vez, a qualquer momento e de mais de uma thread.
#include "test.h"

#include "render/soft/soft_parallel.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace soft = bs::native::render::soft;

BS_TEST(render_parallel_cada_linha_uma_vez) {
    for (const size_t n : {size_t(0), size_t(1), size_t(7), size_t(480), size_t(4096)}) {
        std::vector<int> rows(n);
        for (size_t i = 0; i < n; ++i) rows[i] = int(i);
        for (int rep = 0; rep < 200; ++rep) {  // repetido: um trabalho nao pode vazar para o seguinte
            std::unique_ptr<std::atomic<int>[]> vistos(new std::atomic<int>[n + 1]());
            soft::for_each_row(rows.data(), n, [&](int y) { vistos[size_t(y)].fetch_add(1); });
            int erradas = 0;
            for (size_t i = 0; i < n; ++i) erradas += vistos[i].load() != 1;
            BS_CHECK(erradas == 0);
        }
    }
}

BS_TEST(render_parallel_usa_mais_de_uma_thread) {
    if (std::thread::hardware_concurrency() < 2) return;
    std::vector<int> rows(64);
    for (int i = 0; i < 64; ++i) rows[size_t(i)] = i;
    std::mutex            m;
    std::set<std::thread::id> ids;
    // cada linha espera um pouco: quem pediu nao consegue terminar tudo antes de outra thread pegar trabalho
    soft::for_each_row(rows.data(), rows.size(), [&](int) {
        {
            const std::lock_guard<std::mutex> g(m);
            ids.insert(std::this_thread::get_id());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    });
    BS_CHECK(ids.size() >= 2);
}

// Cada pedido tem o proprio contador, e ele nao pode mudar depois que o pedido voltou: um trabalho de outra geracao, ou o
// corpo de outro pedido, apareceria aqui. Um total unico nao veria a troca.
BS_TEST(render_parallel_varias_threads_pedindo) {
    std::vector<int> rows(100);
    for (int i = 0; i < 100; ++i) rows[size_t(i)] = i;
    constexpr int kThreads = 4, kPedidos = 200;
    std::atomic<int>  contadores[kThreads * kPedidos] = {};
    std::atomic<int>  errados{0};
    std::vector<std::thread> t;
    for (int i = 0; i < kThreads; ++i)
        t.emplace_back([&, i] {
            for (int r = 0; r < kPedidos; ++r) {
                std::atomic<int>& c = contadores[i * kPedidos + r];
                soft::for_each_row(rows.data(), rows.size(), [&c](int) { c.fetch_add(1); });
                if (c.load() != 100) errados.fetch_add(1);
            }
        });
    for (std::thread& x : t) x.join();
    for (auto& c : contadores) errados.fetch_add(c.load() != 100);  // congelados: nada chegou depois do retorno
    BS_CHECK(errados.load() == 0);
}
