// Adapted from cclank/cell-architecture-studio (MIT); see LICENSE.upstream.
pub let cells = [
  {
    id: "plant",
    name: "Plant Cell",
    type: "Eukaryotic Cell",
    accent: "#4f8a3f",
    accentSoft: "#e5f1d8",
    color: "#81b64b",
    defaultOrganelle: "nucleus",
    comparison: "whiteBlood",
    clinicalContext: "A useful non-human baseline for teaching why cell walls and chloroplasts are absent from patient tissue samples.",
    occurrence: {
      title: "Leaves, stems, roots",
      body: "Plant cells form tissues that store energy, move water, and turn sunlight into sugars.",
      motif: "leaf"
    },
    organelles: [
      {
        id: "nucleus",
        name: "Nucleus",
        subtitle: "The control center",
        color: "#7047a8",
        attributes: [
          {
            label: "Size",
            value: "5 to 10 µm in diameter"
          },
          {
            label: "Location",
            value: "Usually central"
          },
          {
            label: "Visible in LM",
            value: "Yes"
          }
        ],
        note: "The nucleus is surrounded by a double membrane called the nuclear envelope, which contains pores that regulate molecular traffic.",
        fact: "The nucleus was one of the first cell structures discovered."
      },
      {
        id: "chloroplast",
        name: "Chloroplast",
        subtitle: "The light harvester",
        color: "#5fa842",
        attributes: [
          {
            label: "Role",
            value: "Photosynthesis"
          },
          {
            label: "Pigment",
            value: "Chlorophyll"
          },
          {
            label: "Visible in LM",
            value: "Often"
          }
        ],
        note: "Chloroplasts convert light energy into chemical energy and give many plant tissues their green color.",
        fact: "A single leaf cell can contain dozens of chloroplasts."
      },
      {
        id: "vacuole",
        name: "Vacuole",
        subtitle: "The pressure reservoir",
        color: "#62bdd2",
        attributes: [
          {
            label: "Volume",
            value: "Large central space"
          },
          {
            label: "Content",
            value: "Water and solutes"
          },
          {
            label: "Function",
            value: "Turgor support"
          }
        ],
        note: "The central vacuole stores water, ions, and small molecules while helping the plant cell remain firm.",
        fact: "Vacuoles can occupy most of a mature plant cell."
      },
      {
        id: "cellWall",
        name: "Cell Wall",
        subtitle: "The rigid frame",
        color: "#7aa647",
        attributes: [
          {
            label: "Material",
            value: "Cellulose rich"
          },
          {
            label: "Position",
            value: "Outer boundary"
          },
          {
            label: "Function",
            value: "Protection"
          }
        ],
        note: "The cell wall gives plant cells their regular shape and protects the membrane beneath it.",
        fact: "Cell walls help plants stand upright without a skeleton."
      }
    ]
  },
  {
    id: "whiteBlood",
    name: "White Blood Cell",
    type: "Immune Cell",
    accent: "#6d78a8",
    accentSoft: "#e6eaf7",
    color: "#b9bfd7",
    defaultOrganelle: "lysosome",
    comparison: "plant",
    clinicalContext: "Immune cell morphology is a bridge from basic cell biology to how clinicians interpret infection, inflammation, and blood smears.",
    occurrence: {
      title: "Blood, lymph, tissues",
      body: "White blood cells move through blood and tissue spaces to identify threats and coordinate immune defense.",
      motif: "blood"
    },
    organelles: [
      {
        id: "lysosome",
        name: "Lysosome",
        subtitle: "The cleanup vesicle",
        color: "#8b54b7",
        attributes: [
          {
            label: "Size",
            value: "About 1 µm"
          },
          {
            label: "Content",
            value: "Digestive enzymes"
          },
          {
            label: "Role",
            value: "Breakdown"
          }
        ],
        note: "Lysosomes help immune cells digest engulfed material and recycle worn cellular components.",
        fact: "White blood cells rely heavily on vesicles for defense."
      },
      {
        id: "nucleus",
        name: "Lobed Nucleus",
        subtitle: "Flexible genome vault",
        color: "#6f35a1",
        attributes: [
          {
            label: "Shape",
            value: "Often lobed"
          },
          {
            label: "Location",
            value: "Central"
          },
          {
            label: "Visible in LM",
            value: "Yes, with stain"
          }
        ],
        note: "Many white blood cells have a lobed nucleus that helps them squeeze through tight spaces.",
        fact: "Nuclear shape is one clue used to identify immune cell types."
      },
      {
        id: "granules",
        name: "Granules",
        subtitle: "The chemical packets",
        color: "#c06696",
        attributes: [
          {
            label: "Content",
            value: "Proteins and enzymes"
          },
          {
            label: "Use",
            value: "Defense"
          },
          {
            label: "Visibility",
            value: "Stain dependent"
          }
        ],
        note: "Granules store molecules that help immune cells respond quickly to infection or inflammation.",
        fact: "Some immune cells are named by how their granules stain."
      }
    ]
  },
  {
    id: "muscle",
    name: "Muscle Cell",
    type: "Muscle Fiber",
    accent: "#bd514d",
    accentSoft: "#f5dfdc",
    color: "#ca6678",
    defaultOrganelle: "myofibril",
    comparison: "plant",
    clinicalContext: "Muscle fiber architecture links microscopic structure with contraction, fatigue, rehabilitation, and neuromuscular disease teaching.",
    occurrence: {
      title: "Skeletal muscles",
      body: "Muscle fibers contain repeating contractile bundles that shorten to generate force.",
      motif: "muscle"
    },
    organelles: [
      {
        id: "myofibril",
        name: "Myofibril",
        subtitle: "The contracting thread",
        color: "#bd3d51",
        attributes: [
          {
            label: "Diameter",
            value: "About 1 µm"
          },
          {
            label: "Arrangement",
            value: "Striated bundles"
          },
          {
            label: "Visible in LM",
            value: "Yes, banded"
          }
        ],
        note: "Each muscle fiber contains hundreds to thousands of myofibrils running its full length, packed tightly together.",
        fact: "A single muscle fiber can be up to 30 cm long."
      },
      {
        id: "sarcolemma",
        name: "Sarcolemma",
        subtitle: "The excitable membrane",
        color: "#d7b284",
        attributes: [
          {
            label: "Position",
            value: "Outer surface"
          },
          {
            label: "Role",
            value: "Signal spread"
          },
          {
            label: "Type",
            value: "Cell membrane"
          }
        ],
        note: "The sarcolemma conducts electrical signals that trigger contraction throughout the muscle fiber.",
        fact: "Membrane signals reach deep into fibers through T tubules."
      },
      {
        id: "mitochondria",
        name: "Mitochondria",
        subtitle: "The endurance supply",
        color: "#cf7042",
        attributes: [
          {
            label: "Role",
            value: "Energy supply"
          },
          {
            label: "Position",
            value: "Between fibers"
          },
          {
            label: "Density",
            value: "Activity dependent"
          }
        ],
        note: "Muscle cells need many mitochondria because contraction consumes large amounts of ATP.",
        fact: "Endurance training can increase mitochondrial density."
      }
    ]
  }
]

pub fn cell(id) => ([for (c in cells where c.id == id) c])[0]
pub fn organelle(id, part) => ([for (o in cell(id).organelles where o.id == part) o])[0]
