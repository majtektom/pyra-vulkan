varying float intensity_sq, intensity;

uniform sampler2D s_texture_0;
uniform sampler2D s_texture_1;

uniform vec4 horizon;

void main()
{
   vec4 clouds_0 = texture2D(s_texture_0, gl_TexCoord[0].xy);
   vec4 clouds_1 = texture2D(s_texture_1, gl_TexCoord[0].xy);
	
   vec4 clouds = (clouds_0 + clouds_1) * intensity_sq;

   vec4 cloud_color = vec4((1.0 - intensity) * horizon.x, (1.0 - intensity) * horizon.y, intensity * horizon.z, 0.0);

   gl_FragColor = cloud_color * (1.0 - clouds.x) + clouds;
}
