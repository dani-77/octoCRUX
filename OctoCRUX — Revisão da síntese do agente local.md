# OctoCRUX — Revisão da síntese do agente local

Oct 19, 2026 · @Daniel Azevedo

Os dois passos propostos pelo agente local (fixtures e atualização da documentação) podem ser aprovados, depois de corrigidas cinco imprecisões da síntese e acrescentados cinco factos que o parser precisa.

## Correções à síntese

| Afirmação do agente | Correção | Estado |
| --- | --- | --- |
| Tabela de estados com quatro combinações de `%i`×`%l` | Faltam `yes`+`yes` (bloqueado e atualizado) e `no`+`no` (não instalado). O alias é um eixo próprio: o modelo tem três eixos (instalação, lock, alias) | Verificado |
| O rust é "bloqueado não instalado" por causa do alias | O lock vem do `%l` e o "fornecido por" vem do ficheiro de aliases; são independentes | Verificado |
| O `-if` ignoraria também ficheiros modificados | O footprint só reporta NEW e MISSING; uma mudança de permissões ou dono aparece como par MISSING+NEW. A conclusão (`-in` é mais estreito) mantém-se | Hipótese a confirmar |
| O `pkgadd` guarda ficheiros `.new` em `rejected/` | Não verificado; de memória, o `rejected/` reproduz a árvore de caminhos sem sufixo `.new` | Hipótese a confirmar |
| A ausência do qtermwidget6 valida a recomendação | Só prova que o caminho sem terminal embutido é obrigatório. Prompts interativos, `doas`/`su` e cancelamento entre uids continuam em aberto | Em aberto |
| A cache é eliminada por completo | 69 ms é o valor neste hardware; confirmar numa máquina mais lenta antes de a declarar desnecessária em geral | Verificado só neste hardware |

## O que ficou de fora e o parser precisa

- O `quickdep` é **recursivo**, inclui o próprio port no fim e tem um espaço a mais.
- Os códigos de saída são ambíguos: o `dependent` de um port inexistente devolve rc=0 vazio, e o `isinst` devolve 255 tanto para "não instalado" como para erro.
- O `prt-get diff` responde com texto humano ("No differences found") mesmo com rc=0.
- O `%P` vem sempre vazio, e o `pkginfo -o` tem cabeçalho e colunas alinhadas.
- Com `rmlog_on_success yes`, os logs em `/var/log/pkgbuild/` identificam quem falhou num `sysup`.
