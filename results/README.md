# Resultados

- `raw/`: saídas grandes de execução. Fica fora do Git e pode ser regenerado por cenário, seed e commit.
- `consolidated/`: tabelas consolidadas usadas no artigo. Deve ser versionado.
- `figures/`: gráficos finais usados no artigo e apresentação. Deve ser versionado.

Registre no manifesto experimental os comandos, seeds, configurações, hash do commit e hashes das cargas/resultados para manter reprodutibilidade.

Os arquivos brutos de uma execução são CSV. O resultado agregado possui uma linha identificada por `run_id` e `workload_sha256`; métricas por processo são opcionais. Arquivos com extensão `.json` ficam reservados ao manifesto experimental da ISSUE-13.

## Executor experimental

`raw/` contém artefatos reproduzíveis, porém potencialmente grandes, e é ignorado pelo
Git. Cada execução de `scripts/run_experiment.py` cria um diretório identificado por
`experiment_id`, com este formato:

```text
raw/<experiment_id>/
├── manifest.json
├── workloads/<cenario>_seed_<seed>.csv
└── runs/<cenario>_<algoritmo>_seed_<seed>.csv
```

O manifesto é a fonte de rastreabilidade e registra identidade do código/configuração,
hashes SHA-256, comandos, horários, tentativas e falhas. Não copie arquivos entre
experimentos: alterações de commit, binário ou configuração exigem outro identificador.

Resultados estatísticos pequenos e revisados pertencem a `consolidated/`; gráficos
finais pertencem a `figures/`.
