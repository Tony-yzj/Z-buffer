#include "scanline.h"

void EdgeListContruct(Vector3 p1, Vector3 p2, int ymax, Polygon* id)
{
    Edge edge;

    // let p1 always be the top point
    if(p1.Y <= p2.Y)
        swap(p1, p2);

    if(p2.Y > IMG_HEIGHT - 1 || p1.Y == p2.Y)
        return;
    
    // assign max and min y
    int min_y = p2.Y;
    int max_y = p1.Y;
    edge.dx = -(p1.X - p2.X)/(p1.Y - p2.Y); 

    if(max_y > IMG_HEIGHT - 1)
    {
        p1.X += edge.dx * (max_y - IMG_HEIGHT);
        max_y = IMG_HEIGHT - 1;
    }

    // neglect parallel-to-x-axis edges
    if(max_y == p2.Y)
        return;

    edge.x = p1.X;

    edge.dy = max_y - min_y;
    edge.id = id;

    assert(edge.dy >= 1);

    // note: horizontal edges need to be considered separately
    // 
    // truncation edges
    if (p1.Y < ymax && p1.Y < IMG_HEIGHT - 1)
    {
        edge.x = p1.X + edge.dx;
        edge.dy -= 1;
        max_y -= 1;
    }
    
    if(max_y > 0 && max_y < IMG_HEIGHT)
        ET[max_y].push_back(edge);
}

Vector3 RoundVertex(Vector3 v)
{
    return Vector3((int)floor(v.X), (int)floor(v.Y), v.Z);
}

void ClearTable()
{
    for(int i = 0; i < PT.size(); i++)
    {
        for(int j = 0; j < PT[i].size(); j++)
        {
            delete PT[i][j];
        }
    }
    PT.clear();
    ET.clear();
    PT.resize(IMG_HEIGHT);
    ET.resize(IMG_HEIGHT);  
}
void ListContruct(Loader* loader)
{
    // Clear PT & ET
    ClearTable();

    for (int i = 0; i < loader->LoadedTriangles.size(); i++)
    {
        Polygon* polygon = new Polygon();
        Triangle triangle = loader->LoadedTriangles[i];
        
        // calculate params
        Vector3 p = triangle.vertices[0].Position;
        Vector3 normal = triangle.vertices[0].Normal;

        polygon->calculate_params(RoundVertex(triangle.vertices[0].Position), RoundVertex(triangle.vertices[1].Position), RoundVertex(triangle.vertices[2].Position), normal);

        #ifdef CULL_ENBALE
        // Cull
        if(normal.dot(cameraDirect) < 0)
            continue;
        #endif
        
        polygon->id = i;

        // edge list construction
        int min_y = INT_MAX, max_y = INT_MIN;
        polygon->depth = 0;
        for (int j = 0; j < triangle.vertices.size(); j++)
        {
            min_y = min(min_y, (int)floor(triangle.vertices[j].Position.Y));
            max_y = max(max_y, (int)floor(triangle.vertices[j].Position.Y));
            polygon->depth += triangle.vertices[j].Position.Z;
        }

        for (int j = 0; j < triangle.vertices.size(); j++)
        {
            Vector3 p1 = RoundVertex(triangle.vertices[j].Position);
            Vector3 p2 = RoundVertex((j == (int)triangle.vertices.size() - 1) ? triangle.vertices[0].Position : triangle.vertices[j + 1].Position);

            EdgeListContruct(p1, p2, max_y, polygon);
        }

        polygon->dy = max_y - min_y;

        polygon->depth /= triangle.vertices.size();
        float scale = (polygon->depth - min_z)/(max_z - min_z);
        assert(scale <= 1);
        polygon->color = Vec3b(255 * scale, 255 * scale, 255 * scale);

        if(max_y >= IMG_HEIGHT)
            max_y = IMG_HEIGHT - 1;

        if(polygon->dy > 0 && max_y > 0 && min_y < IMG_HEIGHT - 1)
            PT[max_y].push_back(polygon);
        else
            delete polygon;
    }
}
void addActiveEdge(vector<ActiveEdge>& AET, Edge edge1, Edge edge2, Polygon* polygon, int y)
{
    ActiveEdge ae;
    assert(edge1.id == edge2.id);

    ae.id = edge1.id;

    // make sure edge1 is always the left edge
    if (edge1.x > edge2.x || (edge1.dx > edge2.dx && edge1.x == edge2.x))
        swap(edge1, edge2);

    ae.xl = edge1.x;
    ae.dxl = edge1.dx;
    ae.dyl = edge1.dy;
    ae.xr = edge2.x;
    ae.dxr = edge2.dx;
    ae.dyr = edge2.dy;

    ae.zl = polygon->c != 0 ?-(polygon->a * ae.xl + polygon->b * y + polygon->d)/polygon->c : polygon->depth;
    ae.dzx = polygon->c != 0 ?-polygon->a / polygon->c : 0;
    ae.dzy = polygon->c != 0 ? polygon->b / polygon->c : 0;

    AET.push_back(ae);
}

void scanLine(float* z_buffer, vector<Polygon*>& APT, vector<ActiveEdge>& AET)
{
    APT.clear();
    AET.clear();
    for (int y = IMG_HEIGHT - 1; y >= 0; y--)
    {
        // if new polygon is in the scan line, add it to active polygon list(APT)
        for (int i = 0; i < (int)PT[y].size(); i++)
        {
            assert(i >= 0);
            Polygon *polygon = PT[y][i];
            // add edges intersected with the scan line into active edge list(AET)
            ActiveEdge ae;
            Edge edge1, edge2;
            ae.id = PT[y][i];

            // find edge in current polygon with max y
            for (int j = 0; j < ET[y].size(); j++)
            {
                if (ET[y][j].id == PT[y][i])
                {
                    edge1 = ET[y][j];
                    edge2 = ET[y][j + 1];
                    break;
                }
            }

            addActiveEdge(AET, edge1, edge2, polygon, y);
            APT.push_back(polygon);
        }

        // initialize z buffer as min_z if there are active edges
        for (int j = 0; j < image.cols; j++)
        {
            z_buffer[j] = -FLT_MAX;
            image.at<Vec3b>(IMG_HEIGHT - 1 - y, j) = Vec3b(244, 234, 226);
        }
        

        // update z buffer and color of pixels in the scan line
        for (int i = 0; i < AET.size(); i++)
        {
            ActiveEdge& ae = AET[i];
            float zx = ae.zl;
            int left = (int)floor(ae.xl), right = (int)floor(ae.xr);

            if(left < 0)
            {
                left = 0;
                zx += ae.dzx * (0 - ae.xl);
            }    
            else if(left > IMG_WIDTH - 1)
                left = IMG_WIDTH - 1;

            if(right < 0)
                right = 0;
            else if(right > IMG_WIDTH - 1)
                right = IMG_WIDTH - 1;

            for(int j = left; j <= right; j++)
            {
                if (zx > z_buffer[j])
                {
                    image.at<Vec3b>((IMG_HEIGHT - 1 - y), j) = ae.id->color;
                    z_buffer[j] = zx;
                }
                zx += ae.dzx;
            }

            // update AET
            AET[i].xl += ae.dxl;
            AET[i].xr += ae.dxr;
            AET[i].zl += ae.dzx * ae.dxl + ae.dzy;
            AET[i].dyl -= 1;
            AET[i].dyr -= 1;

            if (AET[i].dyl < 0 && AET[i].dyr < 0)
            {
                if(y == 0)
                    break;
                
                Edge edge1, edge2;
                int j = 0;
                for(j = 0; j < ET[y-1].size(); j++)
                {
                    if(AET[i].id == ET[y-1][j].id)
                    {
                        edge1 = ET[y-1][j];
                        edge2 = ET[y-1][j+1];
                        break;
                    }
                }
                if(j == ET[y-1].size())
                {
                    AET.erase(AET.begin() + i);
                    i--;
                }
                else
                    addActiveEdge(AET, edge1, edge2, AET[i].id, y);
            }
            else if (AET[i].dyl < 0)
            {
                if (y == 0)
                    break;
                for (int j = 0; j < ET[y-1].size(); j++)
                {
                    if (AET[i].id == ET[y-1][j].id)
                    {
                        Edge edge = ET[y-1][j];
                        AET[i].xl = edge.x;
                        AET[i].dxl = edge.dx;
                        AET[i].dyl = edge.dy;
                        break;
                    }
                }
            }
            else if (AET[i].dyr < 0)
            {
                if (y == 0)
                    break;
                for (int j = 0; j < ET[y - 1].size(); j++)
                {
                    if (AET[i].id == ET[y - 1][j].id)
                    {
                        Edge edge = ET[y - 1][j];
                        AET[i].xr = edge.x;
                        AET[i].dxr = edge.dx;
                        AET[i].dyr = edge.dy;
                        break;
                    }
                }
            }

        }

        // update APT
        for (int i = 0; i < APT.size(); i++)
        {
            if(--APT[i]->dy < 0)
                APT.erase(APT.begin() + i);
        }
    }
}