# Noto Sans (terceiros, SIL OFL 1.1)

Fonte do texto do menu e das legendas no Linux (N9-L, L4), no lugar da Segoe UI que o Windows usa pelo DirectWrite. Embutida no
executável do Linux por `native/text/fonts_embed.S.in`; não entra no build do Windows.

- Origem: https://github.com/notofonts/notofonts.github.io, `fonts/NotoSans/hinted/ttf/` (projeto latin-greek-cyrillic)
- Versão: 2.015 (tabela `name`, ID 5)
- `NotoSans-Regular.ttf` SHA-256: `478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823`
- `NotoSans-SemiBold.ttf` SHA-256: `a4e91fd530ac2b4ef5367240144ff37d7d65d66cf76f2e9a2187b93c676f92d0`
- Licença: SIL Open Font License 1.1 (`OFL.txt`, `cee9892f…`), sem nome reservado. Sem alterações.
- Cobertura conferida (`tests/unit/test_text.cpp`): latino com os acentos de pt, es, fr, de e it, latino estendido, grego,
  cirílico e a pontuação das legendas. Não cobre CJK; as setas ◀ ▶ ▲ ▼ do menu não estão na fonte e são desenhadas.
- Kerning: só em GPOS (sem tabela `kern`). Sem shaping (HarfBuzz), os pares não recebem kerning no Linux.
