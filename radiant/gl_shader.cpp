#include "gl_core.hpp"
#include "../lib/strbuf.h"
#include "../lib/mem.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char* shader_comment_end(const char* p) {
    bool line=p[1]=='/';p+=2;
    while (*p && (line ? *p!='\n' : !(p[0]=='*' && p[1]=='/'))) p++;
    return !line && *p ? p+2 : p;
}
static bool shader_token(const char* start,size_t length,const char* token) {
    return strlen(token)==length && memcmp(start,token,length)==0;
}
// only lexical differences are adapted; the system driver parses and types GLSL.
char* native_gl_shader_source(const char* source,bool fragment) {
    if (!source || strlen(source)>1024u*1024u) return nullptr;
    const char* version=nullptr;unsigned line=1,version_line=1,es_version=100;
    bool line_start=true;
    for (const char* p=source;*p;) {
        if (p[0]=='/' && (p[1]=='/' || p[1]=='*')) {
            const char* end=shader_comment_end(p);
            while (p<end) { if (*p++=='\n') { line++;line_start=true; } }
        } else if (*p=='\n') { line++;line_start=true;p++; }
        else if (line_start && isspace((unsigned char)*p)) p++;
        else if (line_start && *p=='#') {
            const char* directive=p+1;while (*directive==' ' || *directive=='\t') directive++;
            if (!strncmp(directive,"version",7) && isspace((unsigned char)directive[7])) {
                version=p;version_line=line;char* tail=nullptr;
                long number=strtol(directive+7,&tail,10);while (*tail==' ' || *tail=='\t') tail++;
                if (number==100) es_version=100;
                else if (number==300 && !strncmp(tail,"es",2) && !isalnum((unsigned char)tail[2])) es_version=300;
                else return mem_strdup(source,MEM_CAT_RENDER); // explicit desktop source bypasses ES adaptation
                break;
            }
            line_start=false;p++;
        } else { line_start=false;p++; }
    }
    StrBuf* out=strbuf_new();if (!out) return nullptr;
    auto prefix=[&](unsigned next_line) {
        strbuf_append_str(out,"#version 330 core\n#define RADIANT_WEBGL_ES 1\n");
        if (fragment && es_version==100) strbuf_append_str(out,"out vec4 radiant_frag_color;\n");
        strbuf_append_format(out,"#line %u\n",next_line);
    };
    if (!version) prefix(1);
    const char* p=source;bool precision=false;
    while (*p) {
        if (p==version) {
            prefix(version_line+1);
            while (*p && *p!='\n') p++;
            if (*p) p++;
        } else if (p[0]=='/' && (p[1]=='/' || p[1]=='*')) {
            const char* end=shader_comment_end(p);strbuf_append_str_n(out,p,end-p);p=end;
        } else if (isalpha((unsigned char)*p) || *p=='_') {
            const char* start=p++;while (isalnum((unsigned char)*p) || *p=='_') p++;
            size_t length=p-start;const char* replacement=nullptr;
            bool qualifier=shader_token(start,length,"highp") || shader_token(start,length,"mediump") || shader_token(start,length,"lowp");
            if (shader_token(start,length,"precision")) precision=true;
            if (shader_token(start,length,"GL_ES")) replacement="RADIANT_WEBGL_ES";
            else if (shader_token(start,length,"__VERSION__")) replacement=es_version==300?"300":"100";
            else if (es_version==100) {
                if (shader_token(start,length,"attribute")) replacement="in";
                else if (shader_token(start,length,"varying")) replacement=fragment?"in":"out";
                else if (shader_token(start,length,"gl_FragColor")) replacement="radiant_frag_color";
                else if (shader_token(start,length,"texture2D") || shader_token(start,length,"textureCube")) replacement="texture";
            }
            // blanks preserve precision declarations' line and column positions.
            if (precision || qualifier) strbuf_append_char_n(out,' ',length);
            else if (replacement) strbuf_append_str(out,replacement);
            else strbuf_append_str_n(out,start,length);
        } else {
            char ch=*p++;strbuf_append_char(out,precision && ch!='\n'?' ':ch);
            if (ch==';') precision=false;
        }
    }
    char* adapted=mem_strdup(out->str,MEM_CAT_RENDER);strbuf_free(out);return adapted;
}
