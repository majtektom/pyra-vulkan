#include "object/terrain/terrain_index_generator.h"

namespace engine3d {

TerrainIndexGenerator::TerrainIndexGenerator(int gridResolution)
    : gridRes_(gridResolution) {}

int TerrainIndexGenerator::computeStitchConfig(
    bool needLeftStitch, bool needRightStitch,
    bool needBottomStitch, bool needTopStitch) 
{
    int config = 0;

    if (needLeftStitch)   config = 1;
    if (needTopStitch)    config = 4;
    if (needRightStitch)  config = 2;
    if (needBottomStitch) config = 3;

    if (needLeftStitch && needRightStitch)                               config = 5;
    if (needBottomStitch && needTopStitch)                                config = 6;
    if (needLeftStitch && needBottomStitch)                               config = 7;
    if (needBottomStitch && needRightStitch)                              config = 8;
    if (needRightStitch && needTopStitch)                                 config = 9;
    if (needTopStitch && needLeftStitch)                                  config = 10;
    if (needLeftStitch && needBottomStitch && needRightStitch)            config = 11;
    if (needBottomStitch && needRightStitch && needTopStitch)             config = 12;
    if (needRightStitch && needTopStitch && needLeftStitch)               config = 13;
    if (needTopStitch && needLeftStitch && needBottomStitch)              config = 14;
    if (needLeftStitch && needRightStitch && needBottomStitch && needTopStitch) config = 15;

    return config;
}

void TerrainIndexGenerator::generate(int lodLevel, int stitchConfig, std::vector<uint32_t>& indices) const {
    int ss = gridRes_;
    int skok = 1 << lodLevel;

    if (skok >= ss) {
        skok = ss;
        for (int z = 0; z < ss; z += skok) {
            for (int x = 0; x < ss; x += skok) {
                indices.push_back(z + x * (ss + 1));
                indices.push_back(z + skok + (x) * (ss + 1));
                indices.push_back(z + (x + skok) * (ss + 1));
                indices.push_back(z + (x + skok) * (ss + 1));
                indices.push_back(z + skok + (x) * (ss + 1));
                indices.push_back(z + skok + (x + skok) * (ss + 1));
            }
        }
        return;
    }

    bool xo = true, xmax = true, zo = true, zmax = true;
    for (int z = 0; z < ss; z += skok) {
        for (int x = 0; x < ss; x += skok) {
            int j = stitchConfig;
            
            				switch( j )
            				{				
            				case 1://z lewej strony l�czenie
            					{
            					if(x==0)
            					{
            					if(xo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					xo=!xo;
            					continue;
            					}
            					}break;
            				case 2://z prawej strony l�czenie
            					{
            					if(x==ss-skok)
            					{
            					if(xmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					xmax=!xmax;
            					continue;
            					}
            					}break;
            				case 3://z do�u l�czenie
            					{
            					if(z==0)
            					{
            					if(zo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zo=!zo;
            					continue;
            					}
            					}break;
            				case 4://z g�ry  l�czenie
            					{
            					if(z==ss-skok)
            					{
            					if(zmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zmax=!zmax;
            					continue;
            					}
            					}break;
            				case 5://z lewej i prawej
            					{
            					if(x==0)
            					{
            					if(xo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					xo=!xo;
            					continue;
            					}
            					if(x==ss-skok)
            					{
            					if(xmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					xmax=!xmax;
            					continue;
            					}
            					}break;
            				case 6://z do�u i g�ry
            					{
            					if(z==0)
            					{
            					if(zo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zo=!zo;
            					continue;
            					}
            					if(z==ss-skok)
            					{
            					if(zmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zmax=!zmax;
            					continue;
            					}
            					}break;
            				case 7://z lewej i do�u
            					{
            					if(x==0)//lewej
            					{
            					if(xo)
            					{
            						if(z!=0)
            						{
            							indices.push_back(z+x*(ss+1));
            							indices.push_back(z+skok+(x+skok)*(ss+1));
            							indices.push_back(z+(x+skok)*(ss+1));
            						}
            						indices.push_back(z+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						
            					}
            					xo=!xo;
            					}
            					if(z==0)//z do�u
            					{
            					if(zo)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zo=!zo;
            					}
            					if(z==0 || x==0)	continue;
            					}break;
            				case 8://z do�u i prawej
            					{
            					if(x==ss-skok)//prawej
            					{
            					if(xmax)
            					{
            					if(z!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					xmax=!xmax;
            					}
            					if(z==0)//z do�u
            					{
            					if(zo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					if(x!=ss-(skok*2))
            					{
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            					}	
            					}
            					zo=!zo;
            					}
            					if((z==(0)) || (x==(ss-skok)))	continue;
            					}break;
            				case 9://z prawej i g�ry
            					{
            					if(x==ss-skok)//prawej
            					{
            					if(xmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					if(z<ss-(skok*2))
            						{
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						}
            					}
            					xmax=!xmax;
            					}
            					if(z==ss-skok)//g�ry
            					{
            					if(zmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					if(x<ss-(skok*2))
            						{
            						indices.push_back(z+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            						}
            					}
            					zmax=!zmax;
            					}
            					if((z==(ss-skok)) || (x==(ss-skok)))	continue;
            					}break;
            				case 10:// z g�ry i lewej
            					{
            					if(x==0)//lewej
            					{
            					if(xo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					if(z!=ss-(skok*2))
            						{
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						}
            					}
            					xo=!xo;
            					}
            					if(z==ss-skok)//g�ry
            					{
            					if(zmax)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zmax=!zmax;
            					}
            					if((z==(ss-skok)) || (x==0))	continue;
            					}break;
            				case 11://z lewej i do�u i prawej
            					{
            					if(x==0)//lewej
            					{
            					if(xo)
            					{
            						if(z!=0)
            						{
            							indices.push_back(z+x*(ss+1));
            							indices.push_back(z+skok+(x+skok)*(ss+1));
            							indices.push_back(z+(x+skok)*(ss+1));
            						}
            						indices.push_back(z+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					xo=!xo;
            					}
            					if(z==0)//z do�u
            					{
            					if(zo)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					if(x!=(ss-(skok*2)))
            						{
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            						}
            					}
            					zo=!zo;
            					}
            					if(x==ss-skok)//prawej
            					{
            					if(xmax)
            					{
            						if(z!=0)//ss-(skok*2))
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					xmax=!xmax;
            					}
            					if(z==0 || x==0 || x==ss-skok)	continue;
            					}break;
            				case 12://z do�u i prawej i g�ry
            					{
            					if(x==ss-skok)//prawej
            					{
            					if(xmax)
            					{
            					if(z!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					if(z!=ss-(skok*2))
            						{
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						}
            					}
            					xmax=!xmax;
            					}
            					if(z==0)//z do�u
            					{
            					if(zo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					if(x!=ss-(skok*2))
            					{
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					}
            					zo=!zo;
            					}
            					if(z==ss-skok)//z g�ry
            					{
            					if(zmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					if(x!=ss-(skok*2))
            						{
            						indices.push_back(z+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            						}
            					}
            					zmax=!zmax;
            					}
            					if((z==(0)) || (x==(ss-skok)) || z==(ss-skok) )	continue;
            					}break;
            				case 13://z prawej i g�ry i lewej
            					{
            					if(x==ss-skok)//prawej
            					{
            					if(xmax)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					if(z<ss-(skok*2))
            						{
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						}
            					}
            					xmax=!xmax;
            					}
            					if(z==ss-skok)//g�ry
            					{
            					if(zmax)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					if(x<ss-(skok*2))
            						{
            						indices.push_back(z+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            						}
            					}
            					zmax=!zmax;
            					}
            					if(x==0)//lewej
            					{
            					if(xo)
            					{
            					indices.push_back(z+x*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					if(z!=ss-(skok*2))
            						{
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						}
            					}
            					xo=!xo;
            					}
            					if((z==(ss-skok)) || (x==(ss-skok)) || x==0)	continue;
            					}break;
            				case 14:// z g�ry i lewej i do�u
            					{
            					if(x==0)//lewej
            					{
            					if(xo)
            					{
            					if(z!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					if(z!=ss-(skok*2))
            						{
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						}
            					}
            					xo=!xo;
            					}
            					if(z==ss-skok)//g�ry
            					{
            					if(zmax)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zmax=!zmax;
            					}
            					if(z==0)//do�u
            					{
            					if(zo)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					zo=!zo;
            					}
            					if((z==(ss-skok)) || (x==0) || z==0)	continue;
            					}break;
            				case 15:// ze wszystkich stron
            					{
            					if(x==ss-skok)//prawej
            					{
            					if(xmax)
            					{
            					if(z!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x+skok)*(ss+1));
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					if(z!=ss-(skok*2))
            						{
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x)*(ss+1));
            						indices.push_back(z+skok*2+(x+skok)*(ss+1));
            						}
            					}
            					xmax=!xmax;
            					}
            					if(z==0)//z do�u
            					{
            					if(zo)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+(x+skok*2)*(ss+1));
            					if(x!=ss-(skok*2))
            					{
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            					}
            					}
            					zo=!zo;
            					}
            					if(z==ss-skok)//z g�ry
            					{
            					if(zmax)
            					{
            					if(x!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+skok+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok*2)*(ss+1));
            					indices.push_back(z+(x+skok)*(ss+1));
            					if(x!=ss-(skok*2))
            						{
            						indices.push_back(z+(x+skok)*(ss+1));
            						indices.push_back(z+skok+(x+skok*2)*(ss+1));
            						indices.push_back(z+(x+skok*2)*(ss+1));
            						}
            					}
            					zmax=!zmax;
            					}
            					if(x==0)//lewej
            					{
            					if(xo)
            					{
            					if(z!=0)
            						{
            						indices.push_back(z+x*(ss+1));
            						indices.push_back(z+skok+(x+skok)*(ss+1));
            						indices.push_back(z+(x+skok)*(ss+1));
            						}
            					indices.push_back(z+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					if(z!=ss-(skok*2))
            					{
            					indices.push_back(z+skok+(x+skok)*(ss+1));
            					indices.push_back(z+skok*2+(x)*(ss+1));
            					indices.push_back(z+skok*2+(x+skok)*(ss+1));
            					}
            					}
            					xo=!xo;
            					}
            					if((z==(0)) || (x==(ss-skok)) || z==(ss-skok) ||x==0)	continue;
            					}break;
            				}
            
            indices.push_back(z + x * (ss + 1));
            indices.push_back(z + skok + (x) * (ss + 1));
            indices.push_back(z + (x + skok) * (ss + 1));
            indices.push_back(z + (x + skok) * (ss + 1));
            indices.push_back(z + skok + (x) * (ss + 1));
            indices.push_back(z + skok + (x + skok) * (ss + 1));
        }
    }

}

} // namespace engine3d

