#include "BspGeometry.h"
#include "bspfile.h"
#include "filesystem.h"
#include <math.h>
#include <limits.h>
#include <string.h>

static_assert(sizeof(dheader_t)==1036 && sizeof(dface_t)==56 && sizeof(texinfo_t)==72,
    "BSP file records must retain their on-disk layout");

namespace {
bool Reject(char *detail,size_t capacity,const char *reason)
{
    snprintf(detail,capacity,"BSP geometry: %s",reason); return false;
}
bool Range(int first,int count,size_t size)
{
    return first>=0 && count>=0 && size_t(first)<=size && size_t(count)<=size-size_t(first);
}
template<class T> bool Lump(const unsigned char *bytes,const dheader_t &header,int id,
    std::vector<T> &result,int version=0)
{
    const lump_t &lump=header.lumps[id];
    if (lump.version!=version || lump.uncompressedSize
        || lump.filelen%sizeof(T)) return false;
    result.resize(lump.filelen/sizeof(T));
    if (!result.empty()) memcpy(result.data(),bytes+lump.fileofs,lump.filelen);
    return true;
}
Vector Position(const BspRenderVertex &vertex)
{
    return Vector(vertex.position[0],vertex.position[1],vertex.position[2]);
}
float Coordinate(const float row[4],const Vector &point)
{
    return row[0]*point.x+row[1]*point.y+row[2]*point.z+row[3];
}
}

bool ReadBspGeometry(const void *data,size_t size,BspRenderGeometry &result,char *detail,size_t capacity)
{
    result.faces.clear();
    if (!data || size<sizeof(dheader_t) || size>16*1024*1024)
        return Reject(detail,capacity,"invalid file size");
    const auto *bytes=static_cast<const unsigned char *>(data);
    dheader_t header; memcpy(&header,bytes,sizeof(header));
    if (header.ident!=IDBSPHEADER || header.version!=BSPVERSION)
        return Reject(detail,capacity,"expected BSP version 21");
    for (int i=0;i<HEADER_LUMPS;++i) {
        const auto &lump=header.lumps[i];
        if (!Range(lump.fileofs,lump.filelen,size) || (lump.filelen && size_t(lump.fileofs)<sizeof(header)))
            return Reject(detail,capacity,"lump outside file");
        if (!lump.filelen) continue;
        for (int j=0;j<i;++j) {
            const auto &other=header.lumps[j];
            if (other.filelen && lump.fileofs<other.fileofs+other.filelen && other.fileofs<lump.fileofs+lump.filelen)
                return Reject(detail,capacity,"overlapping lumps");
        }
    }
    std::vector<dmodel_t> models; std::vector<dface_t> faces;
    std::vector<dvertex_t> vertices; std::vector<dedge_t> edges;
    std::vector<int> surfedges,names; std::vector<texinfo_t> info;
    std::vector<dtexdata_t> textures; std::vector<dplane_t> planes;
    std::vector<ColorRGBExp32> lighting; std::vector<char> strings;
    if (!Lump(bytes,header,LUMP_MODELS,models) || !Lump(bytes,header,LUMP_FACES,faces,LUMP_FACES_VERSION)
        || !Lump(bytes,header,LUMP_VERTEXES,vertices) || !Lump(bytes,header,LUMP_EDGES,edges)
        || !Lump(bytes,header,LUMP_SURFEDGES,surfedges) || !Lump(bytes,header,LUMP_TEXINFO,info)
        || !Lump(bytes,header,LUMP_TEXDATA,textures) || !Lump(bytes,header,LUMP_PLANES,planes)
        || !Lump(bytes,header,LUMP_LIGHTING,lighting,LUMP_LIGHTING_VERSION)
        || !Lump(bytes,header,LUMP_TEXDATA_STRING_DATA,strings) || !Lump(bytes,header,LUMP_TEXDATA_STRING_TABLE,names))
        return Reject(detail,capacity,"unsupported lump version, compression or record size");
    if (models.empty() || models[0].numfaces<1 || models[0].numfaces>256
        || !Range(models[0].firstface,models[0].numfaces,faces.size()))
        return Reject(detail,capacity,"invalid world face range");
    BspRenderGeometry parsed;
    for (int fi=0;fi<models[0].numfaces;++fi) {
        const auto &face=faces[models[0].firstface+fi];
        if (face.numedges<3 || face.numedges>64 || !Range(face.firstedge,face.numedges,surfedges.size())
            || !Range(face.texinfo,1,info.size()) || !Range(face.planenum,1,planes.size()))
            return Reject(detail,capacity,"invalid face indices");
        if (face.dispinfo!=-1 || face.GetNumPrims() || face.side>1 || face.styles[0]!=0
            || face.styles[1]!=255 || face.styles[2]!=255 || face.styles[3]!=255)
            return Reject(detail,capacity,"requires planar faces and one static light style");
        const auto &texinfo=info[face.texinfo];
        if (texinfo.flags || !Range(texinfo.texdata,1,textures.size()))
            return Reject(detail,capacity,"unsupported surface flags or invalid texdata");
        const auto &texture=textures[texinfo.texdata];
        if (texture.width<=0 || texture.height<=0 || !Range(texture.nameStringTableID,1,names.size()))
            return Reject(detail,capacity,"invalid texture dimensions or string index");
        int nameOffset=names[texture.nameStringTableID];
        if (!Range(nameOffset,1,strings.size())) return Reject(detail,capacity,"invalid material name offset");
        const char *name=strings.data()+nameOffset;
        const char *end=static_cast<const char *>(memchr(name,0,strings.size()-nameOffset));
        if (!end || end==name || end-name>=TEXTURE_NAME_LENGTH)
            return Reject(detail,capacity,"unterminated or oversized material name");
        BspRenderFace output; output.material.assign(name,end);
        // Keep material lookups relative to GAME/materials.
        if (output.material[0]=='/' || output.material.find("..")!=std::string::npos
            || output.material.find('\\')!=std::string::npos || output.material.find(':')!=std::string::npos)
            return Reject(detail,capacity,"non-relative material path");
        for (int axis=0;axis<2;++axis) {
            int extent=face.m_LightmapTextureSizeInLuxels[axis];
            if (extent<0 || extent>127) return Reject(detail,capacity,"unsupported lightmap dimensions");
            output.lightmapSize[axis]=extent+1;
            output.lightmapMins[axis]=face.m_LightmapTextureMinsInLuxels[axis];
            output.textureSize[axis]=axis ? texture.height : texture.width;
            memcpy(output.textureVectors[axis],texinfo.textureVecsTexelsPerWorldUnits[axis],sizeof(float)*4);
            memcpy(output.lightmapVectors[axis],texinfo.lightmapVecsLuxelsPerWorldUnits[axis],sizeof(float)*4);
        }
        int count=output.lightmapSize[0]*output.lightmapSize[1];
        if (face.lightofs<0 || face.lightofs%sizeof(ColorRGBExp32)
            || !Range(face.lightofs/sizeof(ColorRGBExp32),count,lighting.size()))
            return Reject(detail,capacity,"light samples outside lighting lump");
        output.lighting.resize(count*4);
        for (int i=0;i<count;++i) {
            const auto &light=lighting[face.lightofs/sizeof(ColorRGBExp32)+i];
            const unsigned char channels[3]={light.r,light.g,light.b};
            for (int c=0;c<3;++c) {
                float value=TexLightToLinear(channels[c],light.exponent);
                if (!isfinite(value) || value<0 || value>4) return Reject(detail,capacity,"light outside supported LDR range");
                output.lighting[i*4+c]=value;
            }
            output.lighting[i*4+3]=1;
        }
        const auto &plane=planes[face.planenum];
        if (!plane.normal.IsValid() || !isfinite(plane.dist) || fabsf(plane.normal.LengthSqr()-1)>1e-3f)
            return Reject(detail,capacity,"invalid face plane");
        unsigned firstVertex=0,previousEnd=0;
        for (int i=0;i<face.numedges;++i) {
            int signedEdge=surfedges[face.firstedge+i];
            if (signedEdge==INT_MIN || !signedEdge || !Range(abs(signedEdge),1,edges.size()))
                return Reject(detail,capacity,"invalid signed surfedge");
            const auto &edge=edges[abs(signedEdge)];
            unsigned start=edge.v[signedEdge<0 ? 1 : 0],finish=edge.v[signedEdge<0 ? 0 : 1];
            if (start>=vertices.size() || finish>=vertices.size() || start==finish || (i && start!=previousEnd))
                return Reject(detail,capacity,"invalid polygon edge chain");
            if (!i) firstVertex=start;
            previousEnd=finish;
            const Vector &point=vertices[start].point;
            if (!point.IsValid() || fabsf(point.x)>32768 || fabsf(point.y)>32768 || fabsf(point.z)>32768 || fabsf(DotProduct(point,plane.normal)-plane.dist)>1e-3f)
                return Reject(detail,capacity,"non-finite or non-planar vertex");
            BspRenderVertex vertex={};
            for (int c=0;c<3;++c) { vertex.position[c]=point[c]; vertex.normal[c]=plane.normal[c]*(face.side ? -1 : 1); }
            for (int axis=0;axis<2;++axis) {
                vertex.uv[axis]=Coordinate(texinfo.textureVecsTexelsPerWorldUnits[axis],point)/(axis ? texture.height : texture.width);
                vertex.luxel[axis]=Coordinate(texinfo.lightmapVecsLuxelsPerWorldUnits[axis],point)-float(face.m_LightmapTextureMinsInLuxels[axis]);
                if (!isfinite(vertex.uv[axis]) || !isfinite(vertex.luxel[axis]) || vertex.luxel[axis]<-.01f
                    || vertex.luxel[axis]>output.lightmapSize[axis]-1+.01f)
                    return Reject(detail,capacity,"invalid texture or lightmap coordinates");
            }
            output.vertices.push_back(vertex);
        }
        if (previousEnd!=firstVertex) return Reject(detail,capacity,"open polygon");
        // A fan is only valid for a convex ordered polygon. Check every vertex
        // against every edge rather than just consecutive turns (star polygons).
        bool nondegenerate=false;
        for (size_t i=0;i<output.vertices.size();++i) {
            Vector a=Position(output.vertices[i]),b=Position(output.vertices[(i+1)%output.vertices.size()]);
            for (const auto &vertex:output.vertices) {
                Vector p=Position(vertex),cross; CrossProduct(b-a,p-a,cross);
                float winding=DotProduct(cross,plane.normal)*(face.side ? -1 : 1);
                if (winding>1e-4f) nondegenerate=true;
                if (winding<-1e-4f)
                    return Reject(detail,capacity,"non-convex or reversed polygon");
            }
        }
        if (!nondegenerate) return Reject(detail,capacity,"degenerate polygon");
        parsed.faces.push_back(output);
    }
    result.faces.swap(parsed.faces); return true;
}

namespace {
bool CheckRejectedFixtures(const std::vector<unsigned char> &original,char *detail,size_t capacity)
{
    dheader_t header; memcpy(&header,original.data(),sizeof(header));
    const char *cases[]={"truncation","magic","version","overlap","compression","record size",
        "surfedge overflow","vertex index","lighting offset","lightmap extent","light style",
        "material terminator","non-finite UV","non-finite vertex","edge chain"};
    for (int test=0;test<int(sizeof(cases)/sizeof(cases[0]));++test) {
        auto bytes=original;
        dheader_t changed=header;
        dface_t face; memcpy(&face,bytes.data()+header.lumps[LUMP_FACES].fileofs,sizeof(face));
        texinfo_t info; memcpy(&info,bytes.data()+header.lumps[LUMP_TEXINFO].fileofs,sizeof(info));
        int bad=INT_MIN;
        switch(test) {
        case 0: bytes.pop_back(); break;
        case 1: changed.ident=0; break;
        case 2: changed.version=20; break;
        case 3: changed.lumps[LUMP_LIGHTING].fileofs=header.lumps[LUMP_FACES].fileofs; break;
        case 4: changed.lumps[LUMP_FACES].uncompressedSize=1024; break;
        case 5: --changed.lumps[LUMP_FACES].filelen; break;
        case 6: memcpy(bytes.data()+header.lumps[LUMP_SURFEDGES].fileofs,&bad,sizeof(bad)); break;
        case 7: {
            dedge_t edge={}; edge.v[0]=65535; edge.v[1]=1;
            memcpy(bytes.data()+header.lumps[LUMP_EDGES].fileofs+sizeof(dedge_t),&edge,sizeof(edge)); break;
        }
        case 8: face.lightofs=INT_MAX; break;
        case 9: face.m_LightmapTextureSizeInLuxels[0]=INT_MAX; break;
        case 10: face.styles[0]=1; break;
        case 11: bytes[header.lumps[LUMP_TEXDATA_STRING_DATA].fileofs+header.lumps[LUMP_TEXDATA_STRING_DATA].filelen-1]='x'; break;
        case 12: info.textureVecsTexelsPerWorldUnits[0][0]=NAN; break;
        case 13: {
            float value=NAN; memcpy(bytes.data()+header.lumps[LUMP_VERTEXES].fileofs,&value,sizeof(value)); break;
        }
        case 14: bad=1; memcpy(bytes.data()+header.lumps[LUMP_SURFEDGES].fileofs+sizeof(int),&bad,sizeof(bad)); break;
        }
        memcpy(bytes.data(),&changed,sizeof(changed));
        memcpy(bytes.data()+header.lumps[LUMP_FACES].fileofs,&face,sizeof(face));
        memcpy(bytes.data()+header.lumps[LUMP_TEXINFO].fileofs,&info,sizeof(info));
        BspRenderGeometry rejected; rejected.faces.resize(1);
        char reason[160]={};
        if (ReadBspGeometry(bytes.data(),bytes.size(),rejected,reason,sizeof(reason)) || !rejected.faces.empty()) {
            snprintf(detail,capacity,"BSP rejection check failed: %s",cases[test]); return false;
        }
    }
    return true;
}
}

bool LoadBspGeometryFixture(BspRenderGeometry &result,char *detail,size_t capacity,bool spatial)
{
    FileHandle_t file=g_pFullFileSystem->Open(spatial ? "maps/ios-spatial.bsp" : "maps/ios-lightmap.bsp","rb","GAME");
    if (file==FILESYSTEM_INVALID_HANDLE) return Reject(detail,capacity,"fixture not found");
    unsigned size=g_pFullFileSystem->Size(file);
    if (size<sizeof(dheader_t) || size>16*1024*1024) {
        g_pFullFileSystem->Close(file); return Reject(detail,capacity,"invalid fixture size");
    }
    std::vector<unsigned char> bytes(size);
    int read=g_pFullFileSystem->Read(bytes.data(),size,file); g_pFullFileSystem->Close(file);
    if (read!=int(size)) return Reject(detail,capacity,"short read");
    if (!ReadBspGeometry(bytes.data(),bytes.size(),result,detail,capacity)) return false;
    // Fixture-only negative checks run once at load, before any GPU allocation.
    return CheckRejectedFixtures(bytes,detail,capacity);
}
