varying float intensity_sq, intensity;

uniform vec4 bbox;
uniform float time;

void main()
{
   gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
   gl_TexCoord[0] = vec4(gl_Vertex.x / 2.0 + time,       gl_Vertex.z / 2.0, 0.0, 0.0);
   gl_TexCoord[1] = vec4(gl_Vertex.x       + time / 2.0, gl_Vertex.z,       0.0, 0.0);

// Get the vertex height and scale it to 0 - 1 range.
   intensity    = gl_Vertex.y /300;// bbox.y;
   intensity    = exp(clamp(intensity, 0.0, 1.0)) / 1.8;
   intensity_sq = intensity * intensity;
}
