# Simulador de Escalonamento de Processos em C

## Descrição

Este projeto consiste em um simulador de escalonamento de processos desenvolvido para a disciplina de Sistemas Operacionais. O objetivo é aplicar conceitos de estados de processos, filas de prontos, bloqueio por E/S, preempção e troca de contexto. O simulador é capaz de gerar cargas de trabalho controladas por *seed*, executar algoritmos clássicos de escalonamento (como FCFS, Round Robin e Prioridade) e comparar os resultados com um algoritmo proposto pela própria equipe, utilizando métricas quantitativas de desempenho e justiça.

## Integrantes

| Nome | GitHub | Contribuições Resumidas |
| --- | --- | --- |
| Elder Rayan Oliveira Silva | @eldrayan | *A definir* |
| Espedito Ramom Mascena Ricarto | @RamomRicarto | *A definir* |
| Manoel Junio Duarte da Silva | @Junio404 | *A definir* |
| Pedro Yan Alcantara Palácio | @pedropalacioo | *A definir* |
| Sabrina Alencar Soares | @sabrinaalencar | *A definir* |
| Samuel Wagner Tiburi Silveira | @samsilveira | *A definir* |
| Sebastião Sousa Soares | @SebastiaoSoares | *A definir* |

## Tecnologias e Mecanismos

- **Linguagem:** C
- *Outras tecnologias e mecanismos a definir.*

## Regras da Simulação

- O simulador utiliza tempo discreto em ticks inteiros de 64 bits.
- Cada processo é uma entidade com atributos como tempo de chegada, prioridade, rajadas de CPU e requisições de E/S.
- Processos em E/S são bloqueados e retornam à fila de prontos após a conclusão.
- Uma troca de contexto só é contada e cobrada na mudança direta entre PIDs diferentes, sem intervalo positivo de ociosidade. Primeiro despacho, saída para ociosidade, despacho após ociosidade e renovação de quantum do único processo apto não contam.
- O custo padrão é 1 tick. Custo zero exige `--allow-zero-context-switch-cost` e identifica a execução como análise complementar.
- Durante a troca, chegadas e conclusões de E/S continuam ocorrendo, mas o processo reservado no início da troca não é substituído.
- O simulador calcula `turnaround = conclusão - chegada`, `tempo ideal = ΣCPU + ΣE/S`, `slowdown = turnaround / tempo ideal` e `Jain(%) = (Σslowdown)² / (n × Σslowdown²) × 100`.
- Tempos e acumuladores usam 64 bits; entrada vazia, denominador zero e operações que excederiam o limite são rejeitados.

## Visualização e Logs

*A definir.*

## Pré-requisitos e Dependências

- `gcc`
- `make`
- Opcional para editores: `bear` ou `compiledb` para gerar `compile_commands.json`
- No Windows deste ambiente, use `mingw32-make` se `make` não estiver no PATH

## Estrutura

```text
src/                 código C
include/             cabeçalhos
configs/             configurações chave=valor
tests/               testes mínimos
scripts/             scripts auxiliares
results/raw/         resultados brutos ignorados pelo Git
results/consolidated resultados consolidados versionados
results/figures      gráficos versionados
docs/                especificações e decisões
artigo/              entregável em formato de artigo
```

## Compilação e Execução

```sh
make                 # build otimizado em bin/simulador
make dev             # build com -g e sanitizers no Linux/CI
make release         # build otimizado com -O2
make test            # autoteste do executável
make simulate        # execução simples documentada
make batch           # exemplo de lote em results/raw/
make graphs          # prepara diretório de figuras
make compile_commands
make clean           # remove build, bin e results/raw/
```

O JSON de saída inclui `mean_turnaround`, `context_switches`, `jain_slowdown_pct` e o vetor `process_metrics`, com turnaround, tempo ideal e slowdown de cada PID. A validação manual da ISSUE-06 está documentada em [`docs/validacao-issue-06.md`](docs/validacao-issue-06.md).

`make clean` não remove `docs/`, `artigo/`, `results/consolidated/` nem `results/figures/`.
