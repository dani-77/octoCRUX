# OctoCRUX — Dependentes, ordem de instalação e aliases

Oct 19, 2026 · @Daniel Azevedo

A segunda corrida do diagnóstico, com a amostra `gtk3`, fecha a semântica do `dependent` e do `quickdep` e revela que os aliases do `prt-get` mudam o que conta como "instalado".

## Verificado com a amostra gtk3

| Comando | Devolve | Resultado com `gtk3` | Uso no OctoCRUX |
| --- | --- | --- | --- |
| `prt-get dependent` | Dependentes diretos **instalados** | 7, todos instalados | Impacto de uma remoção |
| `prt-get dependent --all` | **Todos** os ports que declaram o alvo | 38, dos quais 31 não instalados | Navegação, não remoção |
| `prt-get quickdep` | Ordem de instalação completa e recursiva, com o próprio port no fim | 118 entradas | Plano de transação (perde aliases) |
| `prt-get depends` | A mesma ordem, com cabeçalho e marcas `[i]` | 118 entradas | Mostra os aliases, em texto humano |

- Os 7 resultados sem `--all` são exatamente os ports instalados que têm `gtk3` no `%e`. Ambas as variantes são só diretas, sem recursão.
- O RESEARCH\_FINDINGS tinha a semântica do `--all` ao contrário; a hipótese da revisão confirmou-se.
- O `quickdep` e o `depends` coincidem entrada a entrada, com uma única exceção: o alias da secção seguinte.

## Aliases: o rust é fornecido pelo rust-bin

O `depends` mostra `rust (provided by rust-bin)`, mas o `quickdep` mostra só `rust`. A origem é o `/etc/prt-get.aliases`, que neste sistema contém `rust-bin: rust`, `jdk8-bin: openjdk8`, `librsvg-compat: librsvg` e `mariadb: mysql`. O ficheiro `/var/lib/pkg/prt-get.aliases` não existe aqui.

| Port | Versão | `%i` | `%l` | Coleção |
| --- | --- | --- | --- | --- |
| rust | 1.99.1-1 | no | yes | opt |
| rust-bin | 1.99.1-1 | yes | no | contrib |

Implicações para o OctoCRUX:

- **"Não instalado" não significa "em falta".** O `rust` tem `%i=no`, mas a dependência está satisfeita pelo `rust-bin`. Uma UI ingénua diria que falta o rust.
- **Há três tipos de lock:** bloqueado e atualizado; bloqueado e desatualizado (família llvm); e bloqueado sem estar instalado (rust), que impede compilar o port em vez do binário.
- **O OctoCRUX tem de ler o ficheiro de aliases** e mostrar a relação "fornecido por". O `quickdep` perde essa informação e o `depends` só a dá em texto humano.
