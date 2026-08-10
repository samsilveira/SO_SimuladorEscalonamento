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

- O simulador utiliza tempo discreto ou simulação por eventos.
- Cada processo é uma entidade com atributos como tempo de chegada, prioridade, rajadas de CPU e requisições de E/S.
- Processos em E/S são bloqueados e retornam à fila de prontos após a conclusão.
- O simulador realiza análises de métricas como *turnaround médio*, *trocas de contexto* e *Índice de Jain do slowdown*.
- *Outras regras a definir.*

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

`make clean` não remove `docs/`, `artigo/`, `results/consolidated/` nem `results/figures/`.
