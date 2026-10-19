# OctoCRUX — Resultados do diagnóstico CRUX 3.8

Oct 19, 2026 · @Daniel Azevedo

A primeira corrida do `crux_diagnostics.sh` num sistema CRUX real fecha várias perguntas da pesquisa e contradiz algumas suposições do RESEARCH\_FINDINGS. Tudo o que está neste documento é **verificado** salvo indicação em contrário.

| Ferramenta | Versão |
| --- | --- |
| CRUX | 3.8 |
| prt-get | 5.19.9 |
| pkgutils (pkgmk, pkgadd) | 5.40.12 |
| ports | 1.7 |

## Respostas fechadas

- **`printf`:** interpreta `\t` e `\n` e não acrescenta newline próprio. Em 2307 linhas, nenhuma tem campos a mais ou a menos. Os valores de `%i` são `yes`/`no`/`diff`. O `%e` vem separado por vírgulas sem espaços e está vazio em 450 ports. O `%P` é vazio em todos, por isso não serve. A árvore inteira demora 69 ms, o que torna a cache desnecessária.
- **Locks:** ficam em `/var/lib/pkg/prt-get.locker`, um nome por linha. O token `%l` funciona (`yes`/`no`). O RESEARCH\_FINDINGS estava errado neste ponto.
- **Overlays:** o `printf` dá uma só linha por nome, já com a precedência aplicada; por exemplo, o `gdk-pixbuf` vem do `d77crux`, versão 2-2. O `prt-get dup` mostra os conflitos no formato `ganha > perde`, com versões.
- **Localização:** a saída do `diff` e do `pkginfo -i` é idêntica com `pt_PT.utf8` e com `LC_ALL=C`.

## Surpresas que mudam o desenho

1. **O `diff` e o `quickdiff` escondem os pacotes bloqueados.** A discrepância de 4 é exatamente llvm, clang, lld e compiler-rt, que estão bloqueados e desatualizados. O `%i` diz `diff`, mas o `quickdiff` não os lista e o `diff` responde "No differences found". O OctoCRUX deve mostrar "desatualizado mas bloqueado" como estado próprio, combinando `%i` com `%l`.
2. **O `prt-get diff` não serve para parsing.** Sem diferenças, devolve texto humano com rc=0. A fonte certa é o `printf`.
3. **O `quickdep` não dá as dependências diretas.** Inclui o próprio port no fim e tem um espaço a mais (` boehm-gc paper a2ps  `); parece ser a ordem de instalação. Para dependências diretas usa-se o `%e`. O `depends` tem cabeçalho e marcas `[i]`.
4. **Os códigos de saída não distinguem casos.**
   - O `dependent` de um port inexistente devolve rc=0 e nada, exatamente como "sem dependentes". O port tem de ser validado antes.
   - O `isinst` devolve 255 tanto para "não instalado" como para erro. Não usar.
5. **O `pkginfo -o`** tem um cabeçalho `Package  File` e colunas alinhadas, e guarda os caminhos sem `/` inicial.
6. **O `pkgmk` também tem `-in`** (ignorar só ficheiros NEW). É uma opção mais estreita e mais segura que o `-if` para o diálogo de mismatch.
7. **Os pacotes compilados** seguem o formato `nome#versão-release.pkg.tar.zst`, em `/usr/ports/packages`. É isto que o caminho de recuperação via `pkgadd -u` precisa.
8. **Existe `/var/lib/pkg/rejected`.** O `pkgadd` põe aqui as configurações rejeitadas nos upgrades, à espera do `rejmerge`. O OctoCRUX deve assinalá-las depois de cada transação.
9. **Logs por port.** Com `writelog enabled` e `rmlog_on_success yes`, só ficam em `/var/log/pkgbuild/%n.log` os logs dos ports que falharam. Isto dá uma forma fiável de saber quem falhou num `sysup`.

## O ambiente testado

| Aspeto | Valor observado | Implicação |
| --- | --- | --- |
| Work dir | `/usr/ports/work/` em tmpfs de 10 GiB, sem `noexec` | Builds grandes podem esgotar o espaço a meio |
| Ports bloqueados | llvm, clang, lld, compiler-rt, rust | Coincidem com os ports maiores |
| ccache | `/var/cache/ccache`, dono root, sem ficheiros de outros donos | Consistente com compilar sempre via sudo |
| Overlay | `/usr/ports/d77crux` com prioridade máxima | Testa a precedência real |
| Privilégios | qt-sudo 2.4.1, sudo 1.9.17p2, pkexec | qt-sudo disponível como omissão |
| Qt / CMake | Qt 6.12.0, CMake 3.31.12 | Base de build confirmada |
| qtermwidget6 | Não instalado | O caminho sem terminal embutido aplica-se já a esta máquina |

Pergunta em aberto: os ports bloqueados estão bloqueados por não caberem nos 10 GiB do tmpfs?

## Limitações e próximos passos

- **`dependent --all` continua sem resposta.** O glibc foi má amostra: as dependências de core não são declaradas, e ambas as variantes deram só `libxcrypt`. Repetir com um port com muitos dependentes, por exemplo `--installed gtk3`.
- **Cache não testada.** Não existe `prt-get.cache`, por isso o `--cache` e o `prt-cache` deram rc=255 ("Can't open cache file"). Com 69 ms sem cache, é pouco provável que seja necessária.
- **Mensagem do script:** o `%P` aparece como "FAILED" quando é só um campo vazio. A corrigir.
- **Relógio da máquina:** está cerca de 1 h adiantado (os ficheiros chegaram com data no futuro), talvez por o hwclock estar em hora local. Irrelevante para o OctoCRUX.

* [x] Confirmar se a corrida foi no T480s com o sysvd77
* [x] Repetir o diagnóstico com `--installed gtk3`
* [x] Passar estes resultados para o RESEARCH\_FINDINGS e o OPEN\_QUESTIONS como `[verified]`
* [x] Guardar as saídas de `cmd/` como fixtures do nível 1
